#ifndef RAJA_policy_openmp_kernel_For_HPP
#define RAJA_policy_openmp_kernel_For_HPP

#include "RAJA/pattern/kernel/internal.hpp"

namespace RAJA {
namespace internal {

template <camp::idx_t ArgumentId, typename Data, typename... EnclosedStmts>
struct OpenMPTargetForWrapper : public GenericWrapperBase {
  using data_t = camp::decay<Data>;

  data_t data;

  RAJA_INLINE
  constexpr explicit OpenMPTargetForWrapper(data_t &d) : 
    data{d}  {}

  RAJA_INLINE
  void exec() { execute_statement_list<camp::list<EnclosedStmts...>>(data); }

  template <typename InIndexType>
  RAJA_INLINE void operator()(InIndexType i)
  {
    data.template assign_offset<ArgumentId>(i);
    exec();
  }
};

template <camp::idx_t ArgumentId,
          int N,
          typename... EnclosedStmts>
struct StatementExecutor<statement::For<ArgumentId, omp_target_parallel_for_exec<N>, EnclosedStmts...>> {

  template <typename Data>
  static RAJA_INLINE void exec(Data &&data)
  {
    OpenMPTargetForWrapper<ArgumentId, Data, EnclosedStmts...> for_wrapper(data);

    auto len = segment_length<ArgumentId>(data);
    using len_t = decltype(len);

    forall_impl(omp_target_parallel_for_exec<N>{}, TypedRangeSegment<len_t>(0, len), for_wrapper);
  }
};

}
}

#endif // RAJA_policy_openmp_kernel_For_HPP
