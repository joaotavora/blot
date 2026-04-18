#include <algorithm>
#include <numeric>
#include <print>
#include <vector>

std::vector<int> sorted(std::vector<int> v) {
    std::sort(v.begin(), v.end());
    return v;
}

int sum(const std::vector<int>& v) {
    return std::accumulate(v.begin(), v.end(), 0);
}

bool contains(const std::vector<int>& v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

std::vector<int> filtered(const std::vector<int>& v, int threshold) {
    std::vector<int> out;
    std::copy_if(v.begin(), v.end(), std::back_inserter(out),
                 [threshold](int x) { return x > threshold; });
    return out;
}

void print_vector(const std::vector<int>& v) {
    for (int x : v) std::print("{} ", x);
    std::print("\n");
}
