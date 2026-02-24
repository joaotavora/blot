#pragma once

#include <algorithm>
#include <cassert>
#include <iterator>
#include <span>
#include <string_view>

struct hmmm {
  using value_type = std::string_view;
  struct iterator {
    iterator() = default;

    using iterator_category = std::forward_iterator_tag;
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;
    using pointer = const value_type*;
    using reference = const value_type&;

    iterator(const char* start, const char* end)
        : end_{end}, cur_line_{update(start)} {}

    friend bool operator==(const iterator& a, const iterator& b) {
      // Can't use default because it compares cur_line_ by value.
      return a.cur_line_.begin() == b.cur_line_.begin() &&
             a.cur_line_.end() == b.cur_line_.end();
    }

    friend bool operator!=(const iterator& a, const iterator& b) {
      return !(a == b);
    }

    reference operator*() const { return cur_line_; }
    pointer operator->() const { return &cur_line_; }

    // Pre-increment
    iterator& operator++() {
      if (cur_line_.end() == end_) {
        cur_line_ = std::string_view{end_, end_};
        return *this;
      }

      // Move past the newline character
      auto start = cur_line_.end() + 1;

      // If we've reached the end, make this an end iterator
      if (start >= end_) {
        cur_line_ = std::string_view{end_, end_};
        return *this;
      }

      cur_line_ = update(start);
      return *this;
    }

    // Post-increment
    iterator operator++(int) {
      iterator tmp = *this;
      ++(*this);
      return tmp;
    }

   private:
    std::string_view update(const char* start) {
      return std::string_view{start, std::find(start, end_, '\n')};
    }

    // perhaps I should keep a pointer to container
    const char* end_ = nullptr;
    std::string_view cur_line_;
  };

  // Constructor taking a span of chars
  explicit hmmm(std::span<const char> data) : data_(data) {}

  // Begin and end methods
  iterator begin() const {
    return iterator(data_.data(), data_.data() + data_.size());
  }

  iterator end() const {
    const char* end_ptr = data_.data() + data_.size();
    return iterator(end_ptr, end_ptr);
  }

  const std::span<const char>& data() const { return data_; }
  std::span<const char>& data() { return data_; }

 private:
  std::span<const char> data_;
};
