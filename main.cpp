#include <algorithm>
#include <array>
#include <iostream>
#include <list>
#include <vector>
#include <thread>
#include <shared_mutex>
#include <atomic>

constexpr size_t default_cache_size = 17u;

template<size_t _Size = 17u>
class DNSCache {
public:
  DNSCache() = default;
  DNSCache(const DNSCache &) = delete;
  DNSCache(DNSCache &&) = delete;
  DNSCache &operator=(const DNSCache &) = delete;
  DNSCache &operator=(DNSCache &&) = delete;

  using Key = std::string;
  using Value = std::string;
  using Cache_element = std::pair<Key, Value>;

private:
  class Batch {
    std::list<Cache_element> _chain;
    mutable std::shared_mutex _mutex;
  public:
    void remove(const Key &key) {
      std::lock_guard guard(_mutex);
      auto it = std::find_if(_chain.begin(), _chain.end(),
                             [&key](Cache_element &el) { return el.first == key; });
      if (it != _chain.end()) {
        _chain.erase(it);
      }
    }

    bool update(const Key &key, const Value &value) {
      std::lock_guard guard(_mutex);
      auto it = std::find_if(_chain.begin(), _chain.end(),
                             [&key](Cache_element &el) { return el.first == key; });
      if (it != _chain.end()) {
        it->second = value;
        return true;// update
      }
      _chain.emplace_back(key, value);
      return false;// new_value
    }

    Value resolve(const Key &key) const {
      std::shared_lock guard(_mutex);
      auto it = std::find_if(_chain.cbegin(), _chain.cend(),
                             [&key](const Cache_element &el) { return el.first == key; });
      if (it != _chain.cend()) {
        return it->second;
      }
      return {};
    }
  };

  std::mutex _mutex;
  size_t _size = 0u;
  std::string _last_name;
  std::array<Batch, _Size> _data;
  std::hash<Key> _hash;

  Batch &get_batch(const Key &key) {
    size_t num = _hash(key) % _Size;
    return _data[num];
  }

  const Batch &get_batch(const Key &key) const {
    size_t num = _hash(key) % _Size;
    return _data[num];
  }

public:
  static DNSCache &create() {
    static DNSCache<default_cache_size> dns_cache;
    return dns_cache;
  }

  void update(const std::string &name, const std::string &ip) {
    auto& batch = get_batch(name);
    if (batch.resolve(name).empty()) {
      std::lock_guard guard(_mutex);
      if (_size == _Size) {
        get_batch(_last_name).remove(_last_name);
        _size--;
      }
    }
    if (!batch.update(name, ip)) {// if new element
      std::lock_guard guard(_mutex);
      _size++;
      _last_name = name;
    }
  }

  std::string resolve(const std::string &name) const {
    return get_batch(name).resolve(name);
  }
};

void simple_act() {
  auto& dns_table = DNSCache<default_cache_size>::create();
  dns_table.resolve("test");
  for (size_t i = 0; i < 100u; ++i) {
    dns_table.update("test" + std::to_string(i), "127.0.0.1");
  }
  for (size_t i = 0; i < 100u; ++i) {
    auto el = dns_table.resolve("test" + std::to_string(i));
  }
}

void test() {
  std::vector<std::thread> v;
  for (size_t i = 0; i < 100u; ++i) {
    v.emplace_back(simple_act);
  }

  for (auto& t : v) {
    t.join();
  }
}

int main() {
  test(); 

  return 0;
}