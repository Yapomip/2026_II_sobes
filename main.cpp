
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <vector>

struct Config {
  size_t read_time;
  size_t write_time;
  size_t shift_time;
  size_t rewind_time;
  size_t local_memory_limit;

  static Config read_from(std::istream &input) {
    Config config;
    input >> config.read_time >> config.write_time >> config.shift_time >>
        config.rewind_time >> config.local_memory_limit;
    return config;
  }
} config;

using Integer = int;

class ITape {
public:
  virtual ~ITape() = default;
  // read data from tape, from current index
  virtual Integer read() const = 0;
  // write to current index
  virtual void write(Integer value) = 0;
  // go tape to next intdex
  virtual void next() const = 0;
  // go tape to prev index
  virtual void prev() const = 0;
  // rewind next to n
  virtual void rewind_next(size_t n) const = 0;
  // rewind prev to n
  virtual void rewind_prev(size_t n) const = 0;
  // get size of tape
  virtual size_t size() const = 0;
  // get index from conditional zero
  virtual size_t current_index() const = 0;
  // create new tape same type with size
  virtual std::unique_ptr<ITape> clone(size_t size) const = 0;
  // rewind to begin of tape
  // TODO: use next or prev if index near begin
  void to_begin() const { rewind_prev(current_index()); }
  virtual std::ostream &print(std::ostream &out) const = 0;
};

std::ostream &operator<<(std::ostream &out, const ITape &tape) {
  return tape.print(out);
}

class TapeOnVector : public ITape {
public:
  TapeOnVector(size_t size) : data(size) {}
  TapeOnVector(std::vector<Integer> data) : data(data) {}

  Integer read() const override { return data[index]; }
  void write(Integer value) override { data[index] = value; }
  void next() const override { index += 1; }
  void prev() const override { index -= 1; }
  void rewind_next(size_t n) const override { index += n; }
  void rewind_prev(size_t n) const override { index -= n; }
  size_t size() const override { return data.size(); }
  size_t current_index() const override { return index; }
  std::unique_ptr<ITape> clone(size_t size) const override {
    return std::make_unique<TapeOnVector>(size);
  }
  std::ostream &print(std::ostream &out) const override {
    out << "(" << size() << " : " << current_index() << ") ";
    for (auto d : data) {
      out << d << " ";
    }
    return out;
  }

private:
  std::vector<Integer> data;
  mutable size_t index = 0;
};

class ISorter {
public:
  virtual void sort(const ITape &input, ITape &output) const = 0;
};

class Sorter : public ISorter {
private:
  void tape_copy_with_sort_n(const ITape &input, ITape &output,
                             size_t n) const {
    // store to local memory from tape
    for (size_t i = 0; i < n; ++i) {
      local_memory[i] = input.read();
      input.next();
    }
    // sort local memory
    std::sort(local_memory.begin(), local_memory.begin() + n);

    // store to local memory from tape
    for (size_t j = 0; j < n; ++j) {
      output.write(local_memory[j]);
      output.next();
    }
  }

  void tape_merge(const ITape &input1, const ITape &input2, size_t size1,
                  size_t size2, ITape &output) const {
    size_t i1 = 0;
    size_t i2 = 0;
    while (i1 < size1 && i2 < size2) {
      Integer d1 = input1.read();
      Integer d2 = input2.read();
      if (d1 < d2) {
        output.write(d1);
        input1.next();
        i1 += 1;
      } else {
        output.write(d2);
        input2.next();
        i2 += 1;
      }
      output.next();
    }
    while (i1 < size1) {
      Integer d = input1.read();
      output.write(d);
      output.next();
      input1.next();
      i1 += 1;
    }
    while (i2 < size2) {
      Integer d = input2.read();
      output.write(d);
      output.next();
      input2.next();
      i2 += 1;
    }
  }

public:
  void sort(const ITape &input, ITape &output) const override {
    const size_t N = input.size();
    const size_t M = config.local_memory_limit;
    // count of blocks in input
    const size_t num_of_blocks = (N + M - 1) / M;
    // it may be in one function not log2
    const size_t K =
        static_cast<size_t>(std::ceil(std::log2(num_of_blocks))) - 1;
    // count of blocks on one tape
    const size_t num_of_blocks_by_additional_tape = 1 << K;
    // size of additional tapes
    const size_t additional_tapes_size = num_of_blocks_by_additional_tape * M;
    const size_t free_space = additional_tapes_size * 2 - N;
    std::array additional_tapes = {
        input.clone(additional_tapes_size), input.clone(additional_tapes_size),
        input.clone(additional_tapes_size), input.clone(additional_tapes_size)};
    std::array tape_sizes = {static_cast<size_t>(0), static_cast<size_t>(0),
                             static_cast<size_t>(0), static_cast<size_t>(0)};

    local_memory.resize(M);

    // sort in begin
    for (size_t i = 0; i < N;) {
      {
        size_t n = std::min(M, N - i);
        tape_copy_with_sort_n(input, *additional_tapes[0], n);
        tape_sizes[0] += n;
        i += n;
      }
      {
        size_t n = std::min(M, N - i);
        tape_copy_with_sort_n(input, *additional_tapes[1], n);
        tape_sizes[1] += n;
        i += n;
      }
    }
    std::cout << "[\n"
              << *additional_tapes[0] << std::endl
              << *additional_tapes[1] << "\n]" << std::endl;

    // merge intermediate blocks sizes
    for (size_t k = 0; k <= K; ++k) {
      additional_tapes[0]->to_begin();
      additional_tapes[1]->to_begin();
      additional_tapes[2]->to_begin();
      additional_tapes[3]->to_begin();
      for (size_t i = 0; i < 1 << (K - k); ++i) {
        {
          size_t merge_size0 = std::min((1 << k) * M, tape_sizes[0]);
          size_t merge_size1 = std::min((1 << k) * M, tape_sizes[1]);
          tape_merge(*additional_tapes[0], *additional_tapes[1], merge_size0,
                     merge_size1, *additional_tapes[2]);
          tape_sizes[0] -= merge_size0;
          tape_sizes[1] -= merge_size1;
          tape_sizes[2] += merge_size0 + merge_size1;
        }
        {
          size_t merge_size0 = std::min((1 << k) * M, tape_sizes[0]);
          size_t merge_size1 = std::min((1 << k) * M, tape_sizes[1]);

          tape_merge(*additional_tapes[0], *additional_tapes[1], merge_size0,
                     merge_size1, *additional_tapes[3]);
          tape_sizes[0] -= merge_size0;
          tape_sizes[1] -= merge_size1;
          tape_sizes[3] += merge_size0 + merge_size1;
        }
      }
      std::swap(additional_tapes[0], additional_tapes[2]);
      std::swap(additional_tapes[1], additional_tapes[3]);
      std::swap(tape_sizes[0], tape_sizes[2]);
      std::swap(tape_sizes[1], tape_sizes[3]);
    }

    additional_tapes[0]->to_begin();
    additional_tapes[1]->to_begin();
    // final merge
    tape_merge(*additional_tapes[0], *additional_tapes[1], tape_sizes[0],
               tape_sizes[1], output);
  }

private:
  mutable std::vector<Integer> local_memory;
};

int main() {
  std::ifstream config_file("config.txt");
  config = Config::read_from(config_file);

  std::cout << "Config parametrs: " << config.read_time << " "
            << config.write_time << " " << config.rewind_time << " "
            << config.shift_time << std::endl;

  TapeOnVector input({6, 5, 4, 3, 2, 1});
  TapeOnVector output(6);
  Sorter sorter;
  sorter.sort(input, output);
  std::cout << input << std::endl << output << std::endl;
  return 0;
}
