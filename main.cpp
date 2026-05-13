
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
    // check if we all ok
    if (N <= M) {
      local_memory.resize(N);
      tape_copy_with_sort_n(input, output, N);
      return;
    }
    // count of blocks in input
    const size_t count_of_blocks = (N + M - 1) / M;
    // it may be in one function not log2
    const size_t K =
        static_cast<size_t>(std::ceil(std::log2(count_of_blocks))) - 1;
    // count of blocks on one tape
    const size_t num_of_blocks_by_additional_tape = 1 << K;
    // size of additional tapes
    const size_t additional_tape_size = num_of_blocks_by_additional_tape * M;
    // space in additional tapes that dont use
    const size_t free_space = additional_tape_size * 2 - N;
    // the coubt of element in last block in input
    const size_t size_of_last_block = N - (count_of_blocks - 1) * M;
    std::array additional_tapes = {
        input.clone(additional_tape_size), input.clone(additional_tape_size),
        input.clone(additional_tape_size), input.clone(additional_tape_size)};
    std::array tape_sizes = {static_cast<size_t>(0), static_cast<size_t>(0),
                             static_cast<size_t>(0), static_cast<size_t>(0)};

    local_memory.resize(M);

    std::cout << "N " << N << " M " << M << " K " << K << std::endl;
    /*
     * sort in begin
     */

    // if num_of_blocks is even we need to put last 2 separate
    // if num_of_blocks is odd we need to put last block separate
    // because last block may be not full
    const size_t count_full_block = (count_of_blocks - 1) / 2;
    std::cout << "count_full_block " << count_of_blocks << "\ncount_full_block "
              << count_full_block << "\nsize_of_last_block "
              << size_of_last_block << std::endl;
    for (size_t i = 0; i < count_full_block; ++i) {
      tape_copy_with_sort_n(input, *additional_tapes[0], M);
      tape_copy_with_sort_n(input, *additional_tapes[1], M);
    }
    tape_sizes[0] += count_full_block * M;
    tape_sizes[1] += count_full_block * M;
    if (count_of_blocks % 2 == 0) {
      tape_copy_with_sort_n(input, *additional_tapes[0], M);
      tape_copy_with_sort_n(input, *additional_tapes[1], size_of_last_block);
      tape_sizes[0] += M;
      tape_sizes[1] += size_of_last_block;
    } else {
      tape_copy_with_sort_n(input, *additional_tapes[0], size_of_last_block);
      // 0 copy elements from input
      tape_sizes[0] += size_of_last_block;
      tape_sizes[1] += 0;
    }

    std::cout << "tape_sizes 0" << tape_sizes[0] << "\ntape_sizes 1"
              << tape_sizes[1] << std::endl;
    std::cout << "[\n"
              << *additional_tapes[0] << std::endl
              << *additional_tapes[1] << "\n]" << std::endl;

    /*
     * merge intermediate blocks sizes
     */

    for (size_t k = 0; k < K; ++k) {
      additional_tapes[0]->to_begin();
      additional_tapes[1]->to_begin();
      additional_tapes[2]->to_begin();
      additional_tapes[3]->to_begin();

      // size of window in block
      const size_t size_of_window = 1 << k;
      const size_t count_of_windows =
          (count_of_blocks + size_of_window - 1) / size_of_window;
      // last blocks need be hanbeled
      const size_t count_full_window = (count_of_windows - 1) / 4;
      const size_t window_element_size = size_of_window * M;

      std::cout << "size_of_window " << size_of_window << "\ncount_of_windows "
                << count_of_windows << "\ncount_full_window "
                << count_full_window << "\nwindow_element_size"
                << window_element_size << std::endl;

      for (size_t i = 0; i < count_full_window; ++i) {
        tape_merge(*additional_tapes[0], *additional_tapes[1],
                   window_element_size, window_element_size,
                   *additional_tapes[2]);

        tape_merge(*additional_tapes[0], *additional_tapes[1],
                   window_element_size, window_element_size,
                   *additional_tapes[3]);
      }
      const size_t write_element_count =
          count_full_window * window_element_size;
      tape_sizes[0] -= 2 * write_element_count;
      tape_sizes[1] -= 2 * write_element_count;
      tape_sizes[2] = 2 * write_element_count;
      tape_sizes[3] = 2 * write_element_count;

      auto write_lasts_to = [&](size_t index_to) {
        size_t size_0 = std::min(tape_sizes[0], window_element_size);
        size_t size_1 = std::min(tape_sizes[1], window_element_size);
        tape_merge(*additional_tapes[0], *additional_tapes[1], size_0, size_1,
                   *additional_tapes[index_to]);
        tape_sizes[0] -= size_0;
        tape_sizes[1] -= size_1;
        tape_sizes[index_to] += size_0 + size_1;
      };
      write_lasts_to(2);
      write_lasts_to(3);

      std::cout << *additional_tapes[0] << std::endl;
      std::cout << *additional_tapes[1] << std::endl;
      std::cout << *additional_tapes[2] << std::endl;
      std::cout << *additional_tapes[3] << std::endl << std::endl;

      std::swap(additional_tapes[0], additional_tapes[2]);
      std::swap(additional_tapes[1], additional_tapes[3]);
      std::swap(tape_sizes[0], tape_sizes[2]);
      std::swap(tape_sizes[1], tape_sizes[3]);
    }

    additional_tapes[0]->to_begin();
    additional_tapes[1]->to_begin();

    /*
     * final merge
     */

    std::cout << *additional_tapes[0] << std::endl;
    std::cout << *additional_tapes[1] << std::endl;
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

  TapeOnVector input({9, 8, 7, 6, 5, 4, 3, 2, 1});
  TapeOnVector output(input.size());
  Sorter sorter;
  sorter.sort(input, output);
  std::cout << input << std::endl << output << std::endl;
  return 0;
}
