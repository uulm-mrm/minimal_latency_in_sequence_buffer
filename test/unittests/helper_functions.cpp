#include "gtest/gtest.h"

#include "minimal_latency_buffer/minimal_latency_buffer.hpp"

namespace minimal_latency_buffer::test
{

TEST(HelperFunction, RemoveIndicesOrdered)
{
  using T = std::unique_ptr<int>;

  constexpr std::size_t n_elements{10};

  std::vector<T> vec;
  vec.reserve(n_elements);
  std::vector<std::size_t> delete_inds;

  for (std::size_t idx{0}; idx < n_elements; ++idx)
  {
    vec.push_back(std::make_unique<int>(static_cast<int>(idx)));

    if (idx % 2)
    {
      delete_inds.push_back(idx);
    }
  }

  remove_indices(vec,delete_inds.begin(), delete_inds.end());

  for (auto &entry : vec)
  {
    EXPECT_TRUE(*entry % 2 == 0);
  }

}

TEST(HelperFunction, RemoveIndicesNotOrdered)
{
  using T = std::unique_ptr<int>;

  constexpr std::size_t n_elements{10};

  std::vector<T> vec;
  vec.reserve(n_elements);

  for (std::size_t idx{0}; idx < n_elements; ++idx)
  {
    vec.push_back(std::make_unique<int>(static_cast<int>(idx)));
  }

  std::vector<std::size_t> delete_inds {5,2,3,1,4,9,0};

  remove_indices(vec,delete_inds.begin(), delete_inds.end());

  for (auto &entry : vec)
  {
    EXPECT_LT(*entry, 9);
    EXPECT_GT(*entry, 5);
  }

}

TEST(HelperFunction, RemoveIndicesSingle)
{

  using T = std::unique_ptr<int>;

  std::vector<T> vec;
  vec.push_back(std::make_unique<int>(static_cast<int>(0)));
  vec.push_back(std::make_unique<int>(static_cast<int>(1)));

  std::vector<std::size_t> delete_inds {0};

  remove_indices(vec,delete_inds.begin(), delete_inds.end());

  EXPECT_EQ(vec.size(), 1);
  EXPECT_EQ(*vec[0], 1);
}

}
