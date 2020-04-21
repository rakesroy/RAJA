//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-20, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

///
/// Source file containing basic functional tests for atomic operations with forall and views.
///

#include "tests/test-forall-atomic-view.hpp"

#include "../test-forall-atomic-utils.hpp"

#if defined(RAJA_ENABLE_OPENMP)
using OmpAtomicForallViewTypes = Test< camp::cartesian_product<
                                                                 AtomicOmpExecs,
                                                                 AtomicOmpPols,
                                                                 HostResourceList,
                                                                 AtomicTypeList >
                                      >::Types;

INSTANTIATE_TYPED_TEST_SUITE_P( OmpTest,
                                SeqForallAtomicViewFunctionalTest,
                                OmpAtomicForallViewTypes );
#endif
