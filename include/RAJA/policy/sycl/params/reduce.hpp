#ifndef NEW_REDUCE_SYCL_REDUCE_HPP
#define NEW_REDUCE_SYCL_REDUCE_HPP

#include "RAJA/pattern/params/reducer.hpp"

namespace RAJA {
namespace expt {
namespace detail {

#if defined(RAJA_ENABLE_SYCL)

  // Init
  template<typename EXEC_POL, typename OP, typename T>
  camp::concepts::enable_if< type_traits::is_sycl_policy<EXEC_POL> >
  init(Reducer<OP, T>& red) {
//    std::cout << "BRIAN: setting to " << OP::identity() << '\n';
    red.val = OP::identity();
  }

  // Combine
  template<typename EXEC_POL, typename OP, typename T>
  camp::concepts::enable_if< type_traits::is_sycl_policy<EXEC_POL> >
  SYCL_EXTERNAL
  combine(Reducer<OP, T>& out, const Reducer<OP, T>& in) {
//    std::cout << "BRIAN: combining " << out.val << " with " << in.val << '\n';
    out.val = OP{}(out.val, in.val);
//    out.val = OP{}(in.val);
  }

  // Resolve
  template<typename EXEC_POL, typename OP, typename T>
  camp::concepts::enable_if< type_traits::is_sycl_policy<EXEC_POL> >
  resolve(Reducer<OP, T>& red) {
    
    std::cout << "BRIAN: resolving " << red.val << " with " << *red.target << '\n';
    *red.target = OP{}(red.val, *red.target);
  }

#endif

} //  namespace detail
} //  namespace expt
} //  namespace RAJA

#endif //  NEW_REDUCE_SYCL_REDUCE_HPP
