//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-20, Lawrence Livermore National Security, LLC.
//
// Produced at the Lawrence Livermore National Laboratory
//
// LLNL-CODE-689114
//
// All rights reserved.
//
// This file is part of RAJA.
//
// For details about use and distribution, please read RAJA/LICENSE.
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

///
/// Source file containing tests for logical, accessor, min/max, and cas atomic operations
///

#include "tests/test-forall-atomic-ref-other.hpp"

#include "../test-forall-atomic-utils.hpp"

#if defined(RAJA_ENABLE_CUDA)
using CudaAtomicForallRefOtherTypes = Test< camp::cartesian_product<
                                                                  AtomicCudaExecs,
                                                                  AtomicCudaPols,
                                                                  CudaResourceList,
                                                                  AtomicTypeList >
                                        >::Types;

INSTANTIATE_TYPED_TEST_SUITE_P( CudaTest,
                                CudaForallAtomicRefOtherFunctionalTest,
                                CudaAtomicForallRefOtherTypes );
#endif
