// Copyright (c) 2020 Zano Project (https://zano.org/)
// Copyright (c) 2020 Locksmith (acmxddk@gmail.com)
// Copyright (c) 2020 sowle (crypto.sowle@gmail.com)
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#pragma once

//
// This file contains the implementation of L2S membership proof protocol
// and a linkable ring signature scheme based on it.
//

point_t ml2s_rsum_impl(size_t n, size_t N, std::vector<point_t>::const_iterator X_array_bg_it, const std::vector<scalar_t>& c1_array,
  const std::vector<scalar_t>& c3_array, const scalar_t& cn)
{
  if (n == 1)
    return *X_array_bg_it + cn * *(X_array_bg_it + 1);
  
  // n >= 2, N >= 4
  return ml2s_rsum_impl(n - 1, N / 2, X_array_bg_it,         c1_array, c3_array, c1_array[n - 2]) +
    cn * ml2s_rsum_impl(n - 1, N / 2, X_array_bg_it + N / 2, c1_array, c3_array, c3_array[n - 2]);
}

bool ml2s_rsum(size_t n, const std::vector<point_t>& X_array, const std::vector<scalar_t>& c1_array,
  const std::vector<scalar_t>& c3_array, point_t& result)
{
  size_t N = (size_t)1 << n;
  CHECK_AND_ASSERT_MES(n != 0, false, "n == 0");
  CHECK_AND_ASSERT_MES(N == X_array.size(), false, "|X_array| != N, " << X_array.size() << ", " << N);
  CHECK_AND_ASSERT_MES(c1_array.size() == n, false, "|c1_array| != n, " << c1_array.size() << ", " << n);
  CHECK_AND_ASSERT_MES(c3_array.size() == n - 1, false, "|c3_array| != n - 1, " << c3_array.size() << ", " << n - 1);

  result = ml2s_rsum_impl(n, N, X_array.begin(), c1_array, c3_array, c1_array[n - 1]);
  return true;
}

struct ml2s_signature_element
{
  point_t   Z0;
  point_t   T0;
  scalar_t  t0;
  point_t   Z;
  std::vector<scalar_t> r_array; // size = n
  std::vector<point_t>  H_array; // size = n
  point_t   T;
  scalar_t  t;
};

struct ml2s_signature
{
  scalar_t z;
  std::vector<ml2s_signature_element> elements; // size = L
};

size_t log2sz(size_t x)
{
  size_t r = 0;
  while (x > 1)
    x >>= 1, ++r;
  return r;
}

template<typename T>
T invert_last_bit(T v)
{
  return v ^ 1;
}

template<typename T>
bool is_power_of_2(T v)
{
  while (v > 1)
  {
    if (v & 1)
      return false;
    v <<= 1;
  }
  return true;
}

bool ml2s_lnk_sig_gen(const scalar_t& m, const std::vector<point_t>& B_array, const std::vector<scalar_t>& b_array, const std::vector<size_t>& s_array, ml2s_signature& signature, uint8_t* p_err = nullptr)
{
#define CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(cond, err_code) \
  if (!(cond)) { LOG_PRINT_RED("ml2s_lnk_sig_gen: \"" << #cond << "\" is false at " << LOCATION_SS << ENDL << "error code = " << err_code, LOG_LEVEL_3); \
  if (p_err) *p_err = err_code; return false; }
#ifndef NDEBUG
#  define DBG_CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(cond, err_code) CHECK_AND_ASSERT_MES_CUSTOM(cond, false, if (p_err) *p_err = err_code, "ml2s_lnk_sig_gen check failed: " << #cond)
#else
#  define DBG_CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(cond, err_code)
#endif

  // check boundaries
  size_t L = b_array.size();
  size_t N = 2 * B_array.size();
  size_t n = log2sz(N);
  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(s_array.size() == L, 0);
  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(1ull << n == N, 1);
  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(L > 0, 2);
  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(L <= N / 2, 3);

  // initialize signature
  signature.elements.resize(L);
  for (size_t i = 0; i < L; ++i)
  {
    signature.elements[i].H_array.resize(n);
    signature.elements[i].r_array.resize(n);
  }

  std::vector<scalar_t> b_inv_array;
  b_inv_array.reserve(L);
  for (size_t i = 0; i < L; ++i)
    b_inv_array.emplace_back(b_array[i].reciprocal());

  std::vector<point_t> I_array;
  I_array.reserve(L);
  for (size_t i = 0; i < L; ++i)
    I_array.emplace_back(b_inv_array[i] * hash_helper_t::hp(b_array[i] * c_point_G));

  signature.z = hash_helper_t::hs(m, B_array, I_array);
  const scalar_t& z = signature.z;

  auto hash_point_lambda = [&z](const point_t& point) { return point + z * hash_helper_t::hp(point); };

  std::vector<point_t> A_array; // size == L
  A_array.reserve(L);
  for (size_t i = 0; i < L; ++i)
    A_array.emplace_back(c_point_G + z * I_array[i]);

  std::vector<point_t> P_array; // size == N // 2
  P_array.reserve(B_array.size());
  for (size_t i = 0; i < B_array.size(); ++i)
    P_array.emplace_back(hash_point_lambda(B_array[i]));

  point_t Q_shift = hash_helper_t::hs(A_array, P_array) * c_point_G;

  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(P_array.size() * 2 == N, 4);
  std::vector<point_t> X_array(N);
  // X_array = { P_array[0], Q_array[0], P_array[1], Q_array[1], etc. }
  for (size_t i = 0; i < N; ++i)
  {
    if (i % 2 == 0)
      X_array[i] = P_array[i / 2];
    else
      X_array[i] = hash_point_lambda(Q_shift + B_array[i / 2]);
  }

  for (size_t i = 0; i < L; ++i)
  {
    size_t s_idx = s_array[i];
    CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(s_idx < B_array.size() && 2 * s_idx + 1 < X_array.size(), 5);
    point_t Ap = b_inv_array[i] * X_array[2 * s_idx];
    CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(Ap == A_array[i], 6);
  }

  ///

  struct intermediate_element_t
  {
    scalar_t f;
    scalar_t k0;
    scalar_t q;

    size_t M_cnt;
    size_t z;
    size_t h;
    scalar_t a;
    scalar_t x;

    std::vector<point_t> Y_array;
  };

  std::vector<intermediate_element_t> interms(L);


  // challenge c0

  scalar_t e = hash_helper_t::hs(z);

  hash_helper_t::hs_t hsc;
  hsc.add_scalar(e);
  hsc.add_points_array(X_array);

  for (size_t i = 0; i < L; ++i)
  {
    auto& interm = interms[i];
    auto& sel = signature.elements[i];

    sel.Z0 = A_array[i]; // b_inv_array[i] * X_array[2 * s_array[i]] + 0 * X_array[2 * s + 1], as k1 == 0 always
    interm.f.make_random();
    sel.Z = interm.f * sel.Z0;
    interm.k0 = interm.f * b_inv_array[i];
    interm.q.make_random();
    sel.T0 = interm.q * sel.Z0;

    hsc.add_point(sel.Z0);
    hsc.add_point(sel.T0);
    hsc.add_point(sel.Z);
  }

  scalar_t c0 = hsc.calc_hash();

  // challenges c11, c13

  hsc.add_scalar(c0);
  for (size_t i = 0; i < L; ++i)
  {
    auto& interm = interms[i];
    auto& sel = signature.elements[i];

    sel.t0 = interm.q - interm.f * c0;
    interm.M_cnt = N;
    interm.z = 2 * s_array[i];
    interm.h = 2 * s_array[i] + 1; // we already checked s_array elements against X_array.size() above
    interm.a = 1;
    interm.q.make_random(); // new q
    interm.Y_array = X_array;

    sel.H_array[0] = interm.k0 / interm.q * X_array[interm.h]; // H1
    
    hsc.add_scalar(sel.t0);
    hsc.add_point(sel.H_array[0]);
  }

  // challenges c11, c13

#ifndef NDEBUG
  // these vectors are only needed for self-check in the end
  std::vector<scalar_t> c1_array(n);   // counting from 0, so c11 is c1_array[0], will have n elements
  std::vector<scalar_t> c3_array(n-1); // the same, will have n - 1 elements
#endif

  scalar_t ci1 = hsc.calc_hash();
  scalar_t ci3 = hash_helper_t::hs(ci1);

  // ci1, ci3 for i in [2; n] -- corresponds c1_array for i in [1; n - 1], c3_array for i in [1; n - 2]
  for (size_t idx_n = 0; idx_n < n - 1; ++idx_n)
  {
#ifndef NDEBUG
    c1_array[idx_n] = ci1;
    c3_array[idx_n] = ci3;
#endif

    std::vector<const scalar_t*> c_array = { &c_scalar_1, &ci1, &c_scalar_1, &ci3 };

    hsc.add_scalar(ci1);
    for (size_t idx_L = 0; idx_L < L; ++idx_L)
    {
      auto& interm = interms[idx_L];
      auto& sel = signature.elements[idx_L];

      const scalar_t& e_local = *c_array[interm.z % 4];
      const scalar_t& g_local = *c_array[interm.h % 4];

      sel.r_array[idx_n] = interm.q * g_local / e_local; // r_i

      interm.a *= e_local;

      DBG_CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(is_power_of_2(interm.M_cnt), 200);
      interm.M_cnt = interm.M_cnt / 2;
      // TODO: check M_scalar is power of 2

      for (size_t j = 0; j < interm.M_cnt; ++j)
        interm.Y_array[j] = (interm.Y_array[2 * j] + *c_array[(2 * j + 1) % 4] * interm.Y_array[2 * j + 1]) / e_local;

      interm.z /= 2;
      interm.h = invert_last_bit(interm.z);
      interm.q.make_random();
      sel.H_array[idx_n + 1] = interm.k0 / interm.q * interm.Y_array[interm.h]; // H_{i+1}
      
      hsc.add_scalar(sel.r_array[idx_n]);
      hsc.add_point(sel.H_array[idx_n + 1]);
    }

    ci1 = hsc.calc_hash();
    ci3 = hash_helper_t::hs(ci1);
  }

  // challenge cn
#ifndef NDEBUG
  c1_array[n - 1] = ci1;
#endif

  // challenge c
  hsc.add_scalar(ci1);
  for (size_t i = 0; i < L; ++i)
  {
    auto& interm = interms[i];
    auto& sel = signature.elements[i];

    CHECK_AND_FAIL_WITH_ERROR_IF_FALSE((interm.z == 0 && interm.h == 1) || (interm.z == 1 && interm.h == 0), 7);
    const scalar_t& e_local = interm.z == 0 ? c_scalar_1 : ci1;
    const scalar_t& g_local = interm.z == 0 ? ci1 : c_scalar_1;

    sel.r_array[n - 1] = interm.q * g_local / e_local; // r_n

    interm.a *= e_local;
    interm.x = interm.a / interm.k0;

    interm.q.make_random(); // qn

    DBG_CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(interm.k0 * X_array[2 * s_array[i]] == sel.Z, 201);

    point_t W = sel.Z;
    for (size_t j = 0; j < n; ++j)
      W = W + sel.r_array[j] * sel.H_array[j];
    sel.T = interm.q * W;

    hsc.add_scalar(sel.r_array[n - 1]);
    hsc.add_point(sel.T);
  }

  scalar_t c = hsc.calc_hash();
  for (size_t i = 0; i < L; ++i)
  {
    auto& interm = interms[i];
    auto& sel = signature.elements[i];
    sel.t = interm.q - interm.x * c;
  }

  // L2S signature is complete

#ifndef NDEBUG
  // self-check
  for (size_t i = 0; i < L; ++i)
  {
    auto& interm = interms[i];
    auto& sel = signature.elements[i];

    point_t W = sel.Z;
    for (size_t j = 0; j < n; ++j)
      W = W + sel.r_array[j] * sel.H_array[j];

    DBG_CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(sel.T == interm.q * W, 230);

    point_t R;
    R.zero();
    bool r = ml2s_rsum(n, X_array, c1_array, c3_array, R);
    DBG_CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(r, 231);
    DBG_CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(R == interm.x * W, 232);
    DBG_CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(sel.t == interm.q - interm.x * c, 233);
    DBG_CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(sel.t * W + c * R == sel.T, 234);
  }
#endif // #ifndef NDEBUG

  return true;
#undef DBG_CHECK_AND_FAIL_WITH_ERROR_IF_FALSE
#undef CHECK_AND_FAIL_WITH_ERROR_IF_FALSE
} // ml2s_lnk_sig_gen


bool ml2s_lnk_sig_verif(const scalar_t& m, const std::vector<point_t>& B_array, const ml2s_signature& signature, uint8_t* p_err = nullptr,
  std::vector<point_t>* p_I_array = nullptr)
{
#define CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(cond, err_code) \
  if (!(cond)) { LOG_PRINT_RED("ml2s_lnk_sig_verif: \"" << #cond << "\" is false at " << LOCATION_SS << ENDL << "error code = " << err_code, LOG_LEVEL_3); \
  if (p_err) *p_err = err_code; return false; }

  auto hash_point_lambda = [&signature](const point_t& point) { return point + signature.z * hash_helper_t::hp(point); };

  size_t L = signature.elements.size();
  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(L > 0, 0);
  size_t n = signature.elements[0].r_array.size();
  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(n < 32, 4);
  size_t N = (size_t)1 << n;
  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(B_array.size() == N / 2, 5);

  std::vector<point_t> local_I_array;
  if (p_I_array == nullptr)
    p_I_array = &local_I_array;
  p_I_array->resize(L);
  std::vector<point_t> A_array(L);
  
  for (size_t i = 0; i < L; ++i)
  {
    (*p_I_array)[i] = (signature.elements[i].Z0 - c_point_G) / signature.z;
    A_array[i] = signature.elements[i].Z0;
    CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(signature.elements[i].r_array.size() == n, 1);
    CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(signature.elements[i].H_array.size() == n, 2);
  }

  scalar_t z_ = hash_helper_t::hs(m, B_array, *p_I_array);
  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(z_ == signature.z, 3);

  scalar_t e = hash_helper_t::hs(signature.z);

  std::vector<point_t> P_array(B_array.size());
  for (size_t i = 0; i < B_array.size(); ++i)
    P_array[i] = hash_point_lambda(B_array[i]);

  point_t Q_shift = hash_helper_t::hs(A_array, P_array) * c_point_G;

  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(P_array.size() * 2 == N, 6);
  std::vector<point_t> X_array(N);
  // X_array = { P_array[0], Q_array[0], P_array[1], Q_array[1], etc. }
  for (size_t i = 0; i < N; ++i)
  {
    if (i % 2 == 0)
      X_array[i] = P_array[i / 2];
    else
      X_array[i] = hash_point_lambda(Q_shift + B_array[i / 2]);
  }

  // challenge c0
  hash_helper_t::hs_t hs_calculator;
  hs_calculator.reserve(1 + N + 3 * L);
  hs_calculator.add_scalar(e);
  hs_calculator.add_points_array(X_array);
  for (size_t i = 0; i < L; ++i)
  {
    auto& sel = signature.elements[i];
    hs_calculator.add_point(sel.Z0);
    hs_calculator.add_point(sel.T0);
    hs_calculator.add_point(sel.Z);
  }
  e = hs_calculator.calc_hash();
  scalar_t c0 = e;

  // check t0 * Z0 + c0 * Z == T0
  for (size_t i = 0; i < L; ++i)
  {
    auto& sel = signature.elements[i];
    CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(sel.t0 * sel.Z0 + c0 * sel.Z == sel.T0, 7);
  }

  // challenges c11, c13
  std::vector<scalar_t> c1_array; // counting from 0, so c11 is c1_array[0], will have n elements
  std::vector<scalar_t> c3_array; // the same, will have n - 1 elements

  hs_calculator.add_scalar(e);
  for (size_t i = 0; i < L; ++i)
  {
    auto& sel = signature.elements[i];
    hs_calculator.add_scalar(sel.t0);
    hs_calculator.add_point(sel.H_array[0]);
  }
  e = hs_calculator.calc_hash();
  c1_array.emplace_back(e);
  c3_array.emplace_back(hash_helper_t::hs(e));

  // ci1, ci3 for i in [2; n] -- corresponds c1_array for i in [1; n - 1], c3_array for i in [1; n - 2]
  for (size_t i = 1; i < n; ++i)
  {
    hs_calculator.add_scalar(e);
    for (size_t j = 0; j < L; ++j)
    {
      auto& sel = signature.elements[j];
      hs_calculator.add_scalar(sel.r_array[i - 1]);
      hs_calculator.add_point(sel.H_array[i]);
    }
    e = hs_calculator.calc_hash();
    c1_array.emplace_back(e);
    if (i != n - 1)
      c3_array.emplace_back(hash_helper_t::hs(e));
  }

  // challenge c
  hs_calculator.add_scalar(e);
  for (size_t i = 0; i < L; ++i)
  {
    auto& sel = signature.elements[i];
    hs_calculator.add_scalar(sel.r_array[n - 1]);
    hs_calculator.add_point(sel.T);
  }
  scalar_t c = hs_calculator.calc_hash();

  // Rsum
  point_t R = c_point_G;
  CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(ml2s_rsum(n, X_array, c1_array, c3_array, R), 8);

  // final checks
  for (size_t i = 0; i < L; ++i)
  {
    auto& sel = signature.elements[i];
    point_t S = sel.Z;
    for (size_t j = 0; j < n; ++j)
    {
      S = S + sel.r_array[j] * sel.H_array[j];
      CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(!S.is_zero(), 9);
      CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(sel.r_array[j] != 0, 10);
      CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(!sel.H_array[j].is_zero(), 11);
    }

    CHECK_AND_FAIL_WITH_ERROR_IF_FALSE(sel.t * S + c * R == sel.T, 12);
  }

  return true;
#undef CHECK_AND_FAIL_WITH_ERROR_IF_FALSE
}
