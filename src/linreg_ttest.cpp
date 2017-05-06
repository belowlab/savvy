#include "savvy/allele_vector.hpp"
//#include "savvy/armadillo_vector.hpp"
//#include "savvy/ublas_vector.hpp"
#include "savvy/reader.hpp"

#include <random>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <vector>

#include <boost/math/distributions.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/vector.hpp>
#include <boost/numeric/ublas/lu.hpp>


namespace ublas = boost::numeric::ublas;

void tab_delimited_write_floats(std::ostream& os, std::tuple<float, float, float, float>&& fields)
{
  std::cout << std::get<0>(fields) << "\t" << std::get<1>(fields) << "\t" << std::get<2>(fields) << "\t" << std::get<3>(fields) << std::endl;
}

template <typename T>
T square(const T& v) { return v * v; }

auto linreg_ttest_old(const std::vector<float>& x, const std::vector<float>& y)
{
  const std::size_t n = x.size();
  const float s_x     = std::accumulate(x.begin(), x.end(), 0.0f);
  const float s_y     = std::accumulate(y.begin(), y.end(), 0.0f);
  const float s_xx    = std::inner_product(x.begin(), x.end(), x.begin(), 0.0f);
  const float s_xy    = std::inner_product(x.begin(), x.end(), y.begin(), 0.0f);
  const float m       = (n * s_xy - s_x * s_y) / (n * s_xx - s_x * s_x);
  const float b       = (s_y - m * s_x) / n;
  auto fx             = [m,b](float x) { return m * x + b; };
  float se_line       = 0.0f; for (std::size_t i = 0; i < n; ++i) se_line += square(y[i] - fx(x[i]));
  const float x_mean  = s_x / n;
  float se_x_mean     = 0.0f; for (std::size_t i = 0; i < n; ++i) se_x_mean += square(x[i] - x_mean);
  const float dof     = n - 2;
  const float std_err = std::sqrt(se_line / dof) / std::sqrt(se_x_mean);
  float t = m / std_err;
  boost::math::students_t_distribution<float> dist(dof);
  float pval = cdf(complement(dist, std::fabs(t))) * 2;
  return std::make_tuple(m, std_err, t, pval); // slope, std error, t statistic, p value
}

auto linreg_ttest(const std::vector<float>& x, const std::vector<float>& y)
{
  const std::size_t n = x.size();
  float s_x{}; //     = std::accumulate(x.begin(), x.end(), 0.0f);
  float s_y{}; //     = std::accumulate(y.begin(), y.end(), 0.0f);
  float s_xx{}; //    = std::inner_product(x.begin(), x.end(), x.begin(), 0.0f);
  float s_xy{}; //    = std::inner_product(x.begin(), x.end(), y.begin(), 0.0f);
  for (std::size_t i = 0; i < n; ++i)
  {
    s_x += x[i];
    s_y += y[i];
    s_xx += x[i] * x[i];
    s_xy += x[i] * y[i];
  }

  const float m       = (n * s_xy - s_x * s_y) / (n * s_xx - s_x * s_x);
  const float b       = (s_y - m * s_x) / n;
  auto fx             = [m,b](float x) { return m * x + b; };
  const float x_mean  = s_x / n;

  float se_line{};
  float se_x_mean{};
  for (std::size_t i = 0; i < n; ++i)
  {
    se_line += square(y[i] - fx(x[i]));
    se_x_mean += square(x[i] - x_mean);
  }

  const float dof     = n - 2;
  const float std_err = std::sqrt(se_line / dof) / std::sqrt(se_x_mean);
  float t = m / std_err;
  boost::math::students_t_distribution<float> dist(dof);
  float pval = cdf(complement(dist, std::fabs(t))) * 2;

  return std::make_tuple(m, std_err, t, pval); // slope, std error, t statistic, p value
}

auto sp_lin_reg_old(const savvy::compressed_vector<float>& x, const std::vector<float>& y)
{
  const std::size_t n = x.size();
  const float s_x     = std::accumulate(x.begin(), x.end(), 0.0f);
  const float s_y     = std::accumulate(y.begin(), y.end(), 0.0f);
  const float s_xx    = std::inner_product(x.begin(), x.end(), x.begin(), 0.0f);
  float s_xy    = 0.0f; for (auto it = x.begin(); it != x.end(); ++it) s_xy += (*it * y[x.index_data()[it - x.begin()]]);
  const float m       = (n * s_xy - s_x * s_y) / (n * s_xx - s_x * s_x);
  const float b       = (s_y - m * s_x) / n;
  auto fx             = [m,b](float x) { return m * x + b; };
  float se_line       = 0.0f; for (std::size_t i = 0; i < n; ++i) se_line += square(y[i] - fx(x[i]));
  const float x_mean  = s_x / n;
  float se_x_mean     = 0.0f; for (std::size_t i = 0; i < n; ++i) se_x_mean += square(x[i] - x_mean);
  const float dof     = n - 2;
  const float std_err = std::sqrt(se_line / dof) / std::sqrt(se_x_mean);
  float t = m / std_err;
  boost::math::students_t_distribution<float> dist(dof);
  float pval = cdf(complement(dist, std::fabs(t))) * 2;
  return std::make_tuple(m, std_err, t, pval); // slope, std error, t statistic, p value
}

auto linreg_ttest(const savvy::compressed_vector<float>& x, const std::vector<float>& y)
{
  const std::size_t n = x.size();
  float s_x{}; //     = std::accumulate(x.begin(), x.end(), 0.0f);
  float s_xx{}; //    = std::inner_product(x.begin(), x.end(), x.begin(), 0.0f);
  float s_xy{}; //    = std::inner_product(x.begin(), x.end(), y.begin(), 0.0f);

  const auto x_beg = x.begin();
  const auto x_end = x.end();
  const float* x_values = x.value_data();
  const std::size_t* x_indices = x.index_data();
  for (auto it = x_beg; it != x_end; ++it)
  {
    s_x += *it;
    s_xx += (*it) * (*it);
    s_xy += (*it * y[x_indices[it - x_beg]]);
  }


  const float s_y     = std::accumulate(y.begin(), y.end(), 0.0f);
  const float m       = (n * s_xy - s_x * s_y) / (n * s_xx - s_x * s_x);
  const float b       = (s_y - m * s_x) / n;
  auto fx             = [m,b](float x) { return m * x + b; };
  const float x_mean  = s_x / n;

  float se_line{};
  float se_x_mean{};
  for (std::size_t i = 0; i < n; ++i)
  {
    se_line += square(y[i] - fx(x[i]));
    se_x_mean += square(x[i] - x_mean);
  }

  const float dof     = n - 2;
  const float std_err = std::sqrt(se_line / dof) / std::sqrt(se_x_mean);
  float t = m / std_err;
  boost::math::students_t_distribution<float> dist(dof);
  float pval = cdf(complement(dist, std::fabs(t))) * 2;

  return std::make_tuple(m, std_err, t, pval); // slope, std error, t statistic, p value
}

auto multi_lin_reg_ttest(const ublas::vector<float>& geno, const ublas::vector<float>& pheno, const ublas::matrix<double>& covariates)
{
  const ublas::matrix<float> beta;
  return true;
}

//template <typename T1, typename T2>
//auto ublas_lin_reg(T1& x, const T2& y)
//{
//  typedef typename T1::value_type flt_type;
//  const std::size_t n    = x.size();
//  const flt_type s_x     = ubl::sum(x);
//  const flt_type s_y     = ubl::sum(y);
//  const flt_type s_xx    = ubl::inner_prod(x, x);
//  const flt_type s_xy    = ubl::inner_prod(x, y);
//  const flt_type m       = (n * s_xy - s_x * s_y) / (n * s_xx - s_x * s_x);
//  const flt_type b       = (s_y - m * s_x) / n;
//  auto fx                = [m,b](float x) { return m * x + b; };
//  flt_type se_line       = 0.0f; for (std::size_t i = 0; i < n; ++i) se_line += square(y[i] - fx(x[i]));
//  const flt_type y_mean  = s_y / n;
//  flt_type se_y_mean     = 0.0f; for (std::size_t i = 0; i < n; ++i) se_y_mean += square(y[i] - y_mean);
//  const flt_type r2      = 1 - se_line / se_y_mean;
//
//  return std::make_tuple(m, b, r2); // slope, y-intercept, r-squared
//}

void run_simple(const std::string& file_path)
{
  auto start = std::chrono::high_resolution_clock().now();
  savvy::dense_allele_vector<float> x;
  savvy::reader r(file_path);

  std::random_device rnd_device;
  std::mt19937 mersenne_engine(rnd_device());
  std::uniform_int_distribution<int> dist(0, 100);
  auto gen = std::bind(dist, mersenne_engine);

  std::vector<float> y(r.sample_size() * 2);
  generate(y.begin(), y.end(), gen);

  std::cout << "pos\tref\talt\tslope\tse\ttstat\tpval\n";
  std::cout << std::fixed << std::setprecision( 4 );
  while (r.read(x, std::numeric_limits<float>::epsilon()))
  {
    float m, se, tstat, pval;
    std::tie(m, se, tstat, pval) = linreg_ttest(x, y);

    std::cout << x.locus() << "\t" << x.ref() << "\t" << x.alt() << "\t";
    std::cout << m << "\t" << se << "\t" << tstat << "\t" << pval << std::endl;
  }

  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock().now() - start).count();
  std::cout << "elapsed: " << elapsed << "s" << std::endl;
}

void print_matrix(const ublas::matrix<float>& m)
{
  for (auto i = 0; i < m.size1(); i++)
  {
    for (auto j = 0; j < m.size2(); j++)
      std::cout << m(i, j) << " ";
    std::cout << std::endl;
  }
}

template <typename T>
auto invert_matrix(const ublas::matrix<T>& input)
{
  // create a working copy of the input
  ublas::matrix<T> a(input);

  // create a permutation matrix for the LU-factorization
  ublas::permutation_matrix<std::size_t> pm(a.size1());

  // perform LU-factorization
  unsigned long res = lu_factorize(a, pm);
  if (res != 0)
    throw std::invalid_argument("Not invertable");

  ublas::matrix<T> ret(a.size1(), a.size2());

  // create identity matrix of "inverse"
  ret.assign(ublas::identity_matrix<T> (a.size1()));

  // backsubstitute to get the inverse
  lu_substitute(a, pm, ret);

  return ret;
}

const std::vector<float> predictor_1 = {41.9, 43.4, 43.9, 44.5, 47.3, 47.5, 47.9, 50.2, 52.8, 53.2, 56.7, 57.0, 63.5, 65.3, 71.1, 77.0, 77.8};
const std::vector<float> predictor_2 = {29.1, 29.3, 29.5, 29.7, 29.9, 30.3, 30.5, 30.7, 30.8, 30.9, 31.5, 31.7, 31.9, 32.0, 32.1, 32.5, 32.9};
const std::vector<float> response = {251.3, 251.3, 248.3, 267.5, 273.0, 276.5, 270.3, 274.9, 285.0, 290.0, 297.0, 302.5, 304.5, 309.3, 321.7, 330.7, 349.0};

void run_multi(const std::string& file_path)
{
  auto start = std::chrono::high_resolution_clock().now();

  const std::size_t num_rows = response.size();

  ublas::vector<float> y(num_rows);
  std::copy(response.begin(), response.end(), y.begin());


  ublas::matrix<float, ublas::column_major> x(num_rows, 3);
  const std::size_t num_cols = x.size2();
  std::fill((x.begin2() + 0).begin(), (x.begin2() + 0).end(), 1.0);
  std::copy(predictor_1.begin(), predictor_1.end(), (x.begin2() + 1).begin());
  std::copy(predictor_2.begin(), predictor_2.end(), (x.begin2() + 2).begin());

  ublas::matrix<float> beta_variances = invert_matrix<float>(ublas::prod(ublas::trans(x), x));
  ublas::vector<float> beta = ublas::prod(ublas::prod(beta_variances, ublas::trans(x)), y); // ublas::trans(beta) ;
  auto fx             = [&beta](const std::vector<float>& x) { return beta[0] + beta[1] * x[0] + beta[2] * x[1]; };
  std::cout << fx({47, 31}) << std::endl;

  ublas::vector<float> residuals = y - ublas::prod(x, beta);
  float square_error{};
  for (const float& r : residuals)
  {
    std::cout << r << " ";
    square_error += square(r);
  }
  std::cout << std::endl;
  const float mean_square_error = square_error / residuals.size();
  std::cout << mean_square_error << std::endl;

  float se_line{};
  float se_x_mean{};
  for (std::size_t i = 0; i < num_rows; ++i)
  {
    se_line += square(y[i] - fx({x(i, 1), x(i, 2)}));
  }
  const float se_line_mean = se_line / num_rows;

  const float dof = num_rows - num_cols; //n - (k + 1)
  const float variance = se_line / dof;

  ublas::matrix<float> var_cov_mat = variance * beta_variances;



  const float std_err_beta_1 = std::sqrt(var_cov_mat(1, 1));
  const float std_err_beta_2 = std::sqrt(var_cov_mat(2, 2));

  const float b1_t = beta[1] / std_err_beta_1;
  const float b2_t = beta[2] / std_err_beta_2;

  boost::math::students_t_distribution<float> dist(dof);
  float pval1 = cdf(complement(dist, std::fabs(b1_t))) * 2;
  float pval2 = cdf(complement(dist, std::fabs(b2_t))) * 2;

  std::cout << beta[0] << " + " << beta[1] << "x + " << beta[2] << "x + " << ublas::sum(residuals) << std::endl;


  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock().now() - start).count();
  std::cout << "elapsed: " << elapsed << "s" << std::endl;
}


int main(int argc, char** argv)
{
  run_multi(argv[1]);



  return 0;
}