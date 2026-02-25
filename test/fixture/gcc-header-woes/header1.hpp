#pragma once

// The header we want to annotate.
inline int count_positives(const int* arr, int n) {
  int count = 0;
  for (int i = 0; i < n; ++i)
    if (arr[i] > 0) ++count;
  return count;
}
