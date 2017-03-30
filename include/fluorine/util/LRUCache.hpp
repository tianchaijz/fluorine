#pragma once

//---------------------------------------------------------------------------//
// Copyright (c) 2013 Kyle Lutz <kyle.r.lutz@gmail.com>
//
// Distributed under the Boost Software License, Version 1.0
// See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt
//
// See http://boostorg.github.com/compute for more information.
//---------------------------------------------------------------------------//

#include <map>
#include <list>
#include <utility>
#include <functional>

#include <boost/optional.hpp>

namespace fluorine {
namespace util {

// a cache which evicts the least recently used item when it is full
template <class Key, class Value>
class LRUCache {
public:
  typedef Key key_type;
  typedef Value value_type;
  typedef std::list<key_type> list_type;
  typedef std::map<key_type,
                   std::pair<value_type, typename list_type::iterator>>
      map_type;

  using OnInsert      = std::function<void(value_type &v)>;
  using OnAggregation = std::function<void(value_type &lhs, value_type &rhs)>;
  using OnClear       = std::function<void(map_type &m)>;
  using OnEvict       = std::function<void(value_type &v)>;

  LRUCache(size_t capacity, OnInsert oi = nullptr, OnAggregation oa = nullptr,
           OnEvict oe = nullptr, OnClear oc = nullptr)
      : m_capacity(capacity), m_oi(oi), m_oa(oa), m_oe(oe), m_oc(oc) {}

  ~LRUCache() {}

  size_t size() const { return m_map.size(); }

  size_t capacity() const { return m_capacity; }

  bool empty() const { return m_map.empty(); }

  bool contains(const key_type &key) { return m_map.find(key) != m_map.end(); }

  void insert(const key_type &key, value_type value) {
    typename map_type::iterator i = m_map.find(key);
    if (i == m_map.end()) {
      // insert item into the cache, but first check if it is full
      if (size() >= m_capacity) {
        // cache is full, evict the least recently used item
        evict();
      }

      if (m_oi) {
        m_oi(value);
      }

      // insert the new item
      m_list.push_front(key);
      m_map[key] = std::make_pair(std::move(value), m_list.begin());
    } else {
      if (m_oa) {
        m_oa(i->second.first, value);
      }
    }
  }

  boost::optional<value_type> get(const key_type &key) {
    // lookup value in the cache
    typename map_type::iterator i = m_map.find(key);
    if (i == m_map.end()) {
      // value not in cache
      return boost::none;
    }

    // return the value, but first update its place in the most
    // recently used list
    typename list_type::iterator j = i->second.second;
    if (j != m_list.begin()) {
      // move item to the front of the most recently used list
      m_list.erase(j);
      m_list.push_front(key);

      // update iterator in map
      j                       = m_list.begin();
      const value_type &value = i->second.first;
      m_map[key]              = std::make_pair(value, j);

      // return the value
      return value;
    } else {
      // the item is already at the front of the most recently
      // used list so just return it
      return i->second.first;
    }
  }

  void clear() {
    if (m_oc) {
      m_oc(m_map);
    }
    m_map.clear();
    m_list.clear();
  }

private:
  void evict() {
    // evict item from the end of most recently used list
    typename list_type::iterator i = --m_list.end();
    if (m_oe) {
      m_oe(m_map[*i].first);
    }
    m_map.erase(*i);
    m_list.erase(i);
  }

private:
  map_type m_map;
  list_type m_list;
  size_t m_capacity;
  OnInsert m_oi;
  OnAggregation m_oa;
  OnEvict m_oe;
  OnClear m_oc;
};

} // namespace util
} // namespace fluorine
