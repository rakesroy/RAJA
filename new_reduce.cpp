#include <iostream>
#include <numeric>
#include <RAJA/RAJA.hpp>
#include <RAJA/util/Timer.hpp>
#include <camp/tuple.hpp>

#include <typeinfo>
#include <iostream>
#include <string>
#include <utility>

int use_dev = 1;

namespace detail {

  CAMP_SUPPRESS_HD_WARN
  template <typename Fn,
            camp::idx_t... Sequence,
            typename TupleLike,
            typename... Ts>
  CAMP_HOST_DEVICE
  auto constexpr invoke_with_order(TupleLike &&t,
                                                    Fn &&f,
                                                    camp::idx_seq<Sequence...>,
                                                    Ts &&... extra)
  {
    //return f(extra..., camp::get<Sequence>(t.value_refs())...);
    return f(extra..., (*camp::get<Sequence>(t))...);
  }

  CAMP_SUPPRESS_HD_WARN
  template <typename TupleLike, typename Fn, typename... Ts>
  CAMP_HOST_DEVICE
  constexpr auto invoke(TupleLike &&t, Fn &&f, Ts &&... extra)
  {
    return invoke_with_order(
        //camp::forward<TupleLike>(t),
        camp::forward<decltype(t.value_refs())>(t.value_refs()),
        camp::forward<Fn>(f),
        camp::make_idx_seq_t< camp::decay<TupleLike>::c_size::value >{},
        camp::forward<Ts>(extra)...);
  }

  template <typename Op, typename T>
  struct Reducer {
    using op = Op;
    using val_type = T;
    Reducer() {}
    Reducer(T *target_in) : target(target_in), val(op::identity()) {}
    Reducer(const Reducer& rhs) : target(rhs.target), val(op::identity()) {}
    T *target = nullptr;
    T val = op::identity();

    T& init() { 
      val = op::identity();
      return val; // Returning this because void parameter loop expansion is broken
    }
    T& resolve() { 
      target = &val;
      return val; // Returning this because void parameter loop expansion is broken
    }
  };



  //
  //
  // Combine
  //
  //

  // Seq
  template<typename EXEC_POL, typename OP, typename T>
  std::enable_if_t< std::is_same< EXEC_POL, RAJA::seq_exec>::value , bool> // Returning bool because void param loop machine broken.
  combine(Reducer<OP, T>& out, const Reducer<OP, T>& in) {
    out.val = typename Reducer<OP,T>::op{}(out.val, in.val);
    return true;
  }
  // OMP
  template<typename EXEC_POL, typename OP, typename T>
  std::enable_if_t< std::is_same< EXEC_POL, RAJA::omp_parallel_for_exec>::value , bool> // Returning bool because void param loop machine broken.
  combine(Reducer<OP, T>& out, const Reducer<OP, T>& in) {
    out.val = typename Reducer<OP,T>::op{}(out.val, in.val);
    return true;
  }

  // OMP Target
  template<typename EXEC_POL, typename OP, typename T>
  std::enable_if_t< std::is_same< EXEC_POL, RAJA::omp_target_parallel_for_exec_nt>::value , bool> // Returning bool because void param loop machine broken.
  combine(Reducer<OP, T>& out, const Reducer<OP, T>& in) {
    out.val = typename Reducer<OP,T>::op{}(out.val, in.val);
    return true;
  }

  // Base Functions
  template<typename EXEC_POL, typename F_PARAM_T, camp::idx_t... Seq>
  void detail_combine(EXEC_POL, F_PARAM_T& out, const F_PARAM_T& in, camp::idx_seq<Seq...>) {
    camp::make_tuple( (combine<EXEC_POL>( camp::get<Seq>(out.param_tup), camp::get<Seq>(in.param_tup)))...  );
  }
  template<typename EXEC_POL, typename F_PARAM_T>
  void combine(F_PARAM_T& out, const F_PARAM_T& in) {
    detail_combine(EXEC_POL(), out, in, typename F_PARAM_T::params_seq::type{} );
  }

  //
  //
  // Resolve
  //
  //

  // Seq
  template<typename EXEC_POL, typename OP, typename T>
  std::enable_if_t< std::is_same< EXEC_POL, RAJA::seq_exec>::value , bool> // Returning bool because void param loop machine broken.
  resolve(Reducer<OP, T>& red) {
    *red.target = red.val;
    return true;
  }
  // OMP
  template<typename EXEC_POL, typename OP, typename T>
  std::enable_if_t< std::is_same< EXEC_POL, RAJA::omp_parallel_for_exec>::value , bool> // Returning bool because void param loop machine broken.
  resolve(Reducer<OP, T>& red) {
    *red.target = red.val;
    return true;
  }
  // OMP Target
  template<typename EXEC_POL, typename OP, typename T>
  std::enable_if_t< std::is_same< EXEC_POL, RAJA::omp_target_parallel_for_exec_nt>::value , bool> // Returning bool because void param loop machine broken.
  resolve(Reducer<OP, T>& red) {
    *red.target = red.val;
    return true;
  }

  // Base Functions
  template<typename EXEC_POL, typename F_PARAM_T, camp::idx_t... Seq>
  void detail_resove(EXEC_POL, F_PARAM_T& f_params, camp::idx_seq<Seq...>) {
    camp::make_tuple( (resolve<EXEC_POL>( camp::get<Seq>(f_params.param_tup) ))...  );
  }

  template<typename EXEC_POL, typename F_PARAM_T>
  void resolve( F_PARAM_T& f_params ) {
    detail_resove(EXEC_POL(), f_params, typename F_PARAM_T::params_seq::type{} );
  }
  
  //
  //
  // Forall Param Wrapper Type
  //
  //
  template<typename... Params>
  struct FORALL_PARAMS_T {
    using Base = camp::tuple<Params...>;
    Base param_tup;
    
    using c_size = camp::tuple_size<Base>; 

  private:
    template<camp::idx_t... Seq>
    auto m_targets(camp::idx_seq<Seq...>)
      -> decltype( camp::make_tuple( (camp::get<Seq>(param_tup).target)...) ) {
      return camp::make_tuple( (camp::get<Seq>(param_tup).target)...) ;
    }

    template<camp::idx_t... Seq>
    auto m_value_refs(camp::idx_seq<Seq...>)
      -> decltype( camp::make_tuple( (&camp::get<Seq>(param_tup).val)...) ) {
      return camp::make_tuple( (&camp::get<Seq>(param_tup).val)...) ;
    }

    template<camp::idx_t... Seq>
    auto m_init(camp::idx_seq<Seq...>)
      -> decltype( camp::make_tuple( (camp::get<Seq>(param_tup).val)...) ) {
      return camp::make_tuple( (camp::get<Seq>(param_tup).init())...) ;
    }

    template<camp::idx_t... Seq>
    auto m_resolve(camp::idx_seq<Seq...>)
      -> decltype( camp::make_tuple( (camp::get<Seq>(param_tup).resolve())...) ) {
      return camp::make_tuple( (camp::get<Seq>(param_tup).resolve())...) ;
    }


  public:
    using params_seq = camp::make_idx_seq_t< c_size::value >;

    FORALL_PARAMS_T(Params... params) {
      param_tup = camp::make_tuple(params...);
    };

    auto targets() -> decltype(m_targets(params_seq{})) {
      return m_targets(params_seq{});
    }
    auto value_refs() -> decltype(m_value_refs(params_seq{})) {
      return m_value_refs(params_seq{});
    }

    auto initialize() -> FORALL_PARAMS_T { // Returning this because void param loop machine broken.
      m_init(params_seq{});
      return *this;
    }

    auto resolve() -> decltype(m_resolve(params_seq{})){
      return m_resolve(params_seq{});
    }

    void combine(const FORALL_PARAMS_T& rhs){};

    FORALL_PARAMS_T(const FORALL_PARAMS_T& rhs) = default;
    FORALL_PARAMS_T& operator=(FORALL_PARAMS_T &&rhs) = default;
    FORALL_PARAMS_T& operator=(const FORALL_PARAMS_T& rhs) = default;
  };


#if defined(RAJA_ENABLE_TARGET_OPENMP)
  template <typename EXEC_POL, typename B, typename... Params>
  std::enable_if_t< std::is_same< EXEC_POL, RAJA::omp_target_parallel_for_exec_nt>::value >
  forall_param(EXEC_POL&&, int N, B const &body, Params... params)
  {
    FORALL_PARAMS_T<Params...> f_params(params...);

    f_params.initialize();

    #pragma omp declare reduction(                                              \
      combine                                                                   \
      : decltype(f_params)                                                      \
      : combine<EXEC_POL>(omp_out, omp_in) )\
      initializer(omp_priv)

    #pragma omp target data if (use_dev) map(tofrom : f_params) map(to : body)
    {
      #pragma omp target if (use_dev) teams distribute parallel for reduction(combine : f_params)
      for (int i = 0; i < N; ++i) {
        invoke(f_params, body, i);
      }
    }
    resolve<EXEC_POL>(f_params);
  }
#endif
  
#if defined(RAJA_ENABLE_OPENMP)
  template <typename EXEC_POL, typename B, typename... Params>
  std::enable_if_t< std::is_same< EXEC_POL, RAJA::omp_parallel_for_exec>::value >
  forall_param(EXEC_POL&&, int N, B const &body, Params... params)
  {
    FORALL_PARAMS_T<Params...> f_params(params...);

    f_params.initialize();

    #pragma omp declare reduction(          \
      combine                               \
      : decltype(f_params)                  \
      : combine<EXEC_POL>(omp_out, omp_in) )\
      initializer(omp_priv)

    #pragma omp parallel for reduction(combine : f_params)
    for (int i = 0; i < N; ++i) {
      invoke(f_params, body, i);
    }

    resolve<EXEC_POL>(f_params);
  }
#endif

  template <typename EXEC_POL, typename B, typename... Params>
  std::enable_if_t< std::is_same< EXEC_POL, RAJA::seq_exec>::value >
  forall_param(EXEC_POL&&, int N, B const &body, Params... params)
  {
    FORALL_PARAMS_T<Params...> f_params(params...);

    f_params.initialize();

    for (int i = 0; i < N; ++i) {
      invoke(f_params, body, i);
    }

    resolve<EXEC_POL>(f_params);
  }

} // namespace detail
// -------------------------------------------------------------------------------------------------



//
//
// User Facing API.
//
//
template <template <typename, typename, typename> class Op, typename T>
auto Reduce(T *target)
{
  return detail::Reducer<Op<T, T, T>, T>(target);
}

template<typename ExecPol, typename B, typename... Params>
void forall_param(int N, const B& body, Params... params) {
  detail::forall_param(ExecPol(), N, body, params...);
}



//
//
// Main
//
//
int main(int argc, char *argv[])
{
  if (argc < 2) {
    std::cout << "Execution Format: ./executable N\n";
    std::cout << "Example of usage: ./new_reduce 5000\n";
    exit(0);
  }
  int N = atoi(argv[1]);  // 500000;

  double r = 0;
  double m = 5000;
  double ma = 0;

  double *a = new double[N]();
  double *b = new double[N]();

  std::iota(a, a + N, 0);
  std::iota(b, b + N, 0);

#if defined(RAJA_ENABLE_TARGET_OPENMP)
  {
    std::cout << "OMP Target Reduction NEW\n";
    #pragma omp target enter data map(to : a[:N], b[:N])

    RAJA::Timer t;
    t.reset();
    t.start();

    forall_param<RAJA::omp_target_parallel_for_exec_nt>(N,
                 [=](int i, double &r_, double &m_, double &ma_) {
                   r_ += a[i] * b[i];
                   m_ = a[i] < m_ ? a[i] : m_;
                   ma_ = a[i] > m_ ? a[i] : m_;
                 },
                 Reduce<RAJA::operators::plus>(&r),
                 Reduce<RAJA::operators::minimum>(&m),
                 Reduce<RAJA::operators::maximum>(&ma));
    t.stop();
    
    std::cout << "t : " << t.elapsed() << "\n";
    std::cout << "r : " << r << "\n";
    std::cout << "m : "  << m  <<"\n";
    std::cout << "ma : " << ma <<"\n";
  }
#endif

#if defined(RAJA_ENABLE_OPENMP)
  {
    std::cout << "OMP Reduction NEW\n";

    RAJA::Timer t;
    t.reset();
    t.start();

    std::once_flag flagp;
    forall_param<RAJA::omp_parallel_for_exec>(N,
                 [=](int i, double &r_, double &m_, double &ma_) {
                   r_ += a[i] * b[i];

                   m_ = a[i] < m_ ? a[i] : m_;
                   ma_ = a[i] > m_ ? a[i] : m_;
                 },
                 Reduce<RAJA::operators::plus>(&r),
                 Reduce<RAJA::operators::minimum>(&m),
                 Reduce<RAJA::operators::maximum>(&ma));
    t.stop();
    
    std::cout << "t : " << t.elapsed() << "\n";
    std::cout << "r : " << r << "\n";
    std::cout << "m : "  << m  <<"\n";
    std::cout << "ma : " << ma <<"\n";
  }
#endif

  {
    std::cout << "Sequential Reduction NEW\n";

    RAJA::Timer t;
    t.reset();
    t.start();

    forall_param<RAJA::seq_exec>(N,
                 [=](int i, double &r_, double &m_, double &ma_) {
                   r_ += a[i] * b[i];
                   m_ = a[i] < m_ ? a[i] : m_;
                   ma_ = a[i] > m_ ? a[i] : m_;
                 },
                 Reduce<RAJA::operators::plus>(&r),
                 Reduce<RAJA::operators::minimum>(&m),
                 Reduce<RAJA::operators::maximum>(&ma));
    t.stop();
    
    std::cout << "t : " << t.elapsed() << "\n";
    std::cout << "r : " << r << "\n";
    std::cout << "m : "  << m  <<"\n";
    std::cout << "ma : " << ma <<"\n";
  }

  {
    std::cout << "Basic Reduction RAJA\n";
    RAJA::ReduceSum<RAJA::seq_reduce, double> rr(0);
    RAJA::ReduceMin<RAJA::seq_reduce, double> rm(5000);
    RAJA::ReduceMax<RAJA::seq_reduce, double> rma(0);

    RAJA::Timer t;
    t.start();
    RAJA::forall<RAJA::loop_exec>(RAJA::RangeSegment(0, N),
                                                        [=](int i) {
                                                          rr += a[i] * b[i];
                                                          rm.min(a[i]);
                                                          rma.max(a[i]);
                                                        });
    t.stop();

    std::cout << "t : " << t.elapsed() << "\n";
    std::cout << "r : " << rr.get() << "\n";
    std::cout << "m : "  << rm.get()  <<"\n";
    std::cout << "ma : " << rma.get() <<"\n";
  }

  return 0;
}
