#include <algorithm>
#include <map>
#include <print>
#include <string>
#include <vector>

class word_counter {
    std::map<std::string, int> counts_;
public:
    void add(const std::string& word) { ++counts_[word]; }

    int count(const std::string& word) const {
        auto it = counts_.find(word);
        return it != counts_.end() ? it->second : 0;
    }

    std::size_t unique_words() const { return counts_.size(); }

    std::string most_frequent() const {
        if (counts_.empty()) return {};
        auto it = std::max_element(
            counts_.begin(), counts_.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        return it->first;
    }
};

word_counter count_words(const std::vector<std::string>& words) {
    word_counter wc;
    for (const auto& w : words) wc.add(w);
    return wc;
}

void print_stats(const word_counter& wc) {
    std::print("unique={} most_frequent={}\n",
               wc.unique_words(), wc.most_frequent());
}
