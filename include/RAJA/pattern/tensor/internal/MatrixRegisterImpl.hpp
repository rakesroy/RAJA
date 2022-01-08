/*!
 ******************************************************************************
 *
 * \file
 *
 * \brief   RAJA header file defining SIMD/SIMT register operations.
 *
 ******************************************************************************
 */

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-22, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/LICENSE file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

#ifndef RAJA_pattern_tensor_MatrixRegisterImpl_HPP
#define RAJA_pattern_tensor_MatrixRegisterImpl_HPP

#include "camp/camp.hpp"
#include "RAJA/config.hpp"
#include "RAJA/pattern/tensor/MatrixRegister.hpp"
#include "RAJA/pattern/tensor/internal/MatrixMatrixMultiply.hpp"
#include "RAJA/util/BitMask.hpp"

//#define DEBUG_MATRIX_LOAD_STORE


namespace RAJA
{
namespace expt
{

  /*
   * 2D (Matrix) specialization of TensorRegister
   */
  template<typename REGISTER_POLICY, typename T, camp::idx_t ROW_ORD, camp::idx_t COL_ORD, camp::idx_t ROW_SIZE, camp::idx_t COL_SIZE>
  class TensorRegister<REGISTER_POLICY, T, TensorLayout<ROW_ORD, COL_ORD>, camp::idx_seq<ROW_SIZE, COL_SIZE>> :
    public RAJA::internal::expt::TensorRegisterBase<TensorRegister<REGISTER_POLICY, T, TensorLayout<ROW_ORD, COL_ORD>, camp::idx_seq<ROW_SIZE, COL_SIZE>>>
  {
    public:
      using self_type = TensorRegister<REGISTER_POLICY, T, TensorLayout<ROW_ORD, COL_ORD>, camp::idx_seq<ROW_SIZE, COL_SIZE>>;
      using base_type = RAJA::internal::expt::TensorRegisterBase<TensorRegister<REGISTER_POLICY, T, TensorLayout<ROW_ORD, COL_ORD>, camp::idx_seq<ROW_SIZE, COL_SIZE>>>;
      using register_type = Register<T, REGISTER_POLICY>;
      using row_vector_type = VectorRegister<T, REGISTER_POLICY, COL_SIZE>;
      using column_vector_type = VectorRegister<T, REGISTER_POLICY, ROW_SIZE>;
      using register_policy = REGISTER_POLICY;
      using element_type = T;
      using layout_type = TensorLayout<ROW_ORD, COL_ORD>;

      using transpose_tensor_type = TensorRegister<REGISTER_POLICY, T, TensorLayout<!ROW_ORD, !COL_ORD>, camp::idx_seq<ROW_SIZE, COL_SIZE>>;

      using transpose_type = TensorRegister<REGISTER_POLICY, T, layout_type, camp::idx_seq<COL_SIZE, ROW_SIZE>>;
      using product_type = TensorRegister<REGISTER_POLICY, T, layout_type, camp::idx_seq<ROW_SIZE, ROW_SIZE>>;

      static constexpr camp::idx_t s_num_rows = ROW_SIZE;
      static constexpr camp::idx_t s_num_columns = COL_SIZE;




      static constexpr camp::idx_t s_elements_per_register =
          RAJA::internal::expt::RegisterTraits<REGISTER_POLICY,T>::s_num_elem;

      // number of registers to hold entire matrix
      static constexpr camp::idx_t s_num_registers =
          (ROW_SIZE*COL_SIZE) / s_elements_per_register;

      // We only allow matrix sizes that exactly fit in some number of registers
      static_assert((ROW_SIZE*COL_SIZE) == s_num_registers*s_elements_per_register,
          "MatrixRegister must be dimensioned to exactly fit an integer number of registers");

      using log_base2_t = RAJA::LogBase2<s_elements_per_register>;

      static constexpr camp::idx_t s_shift_per_register =
          log_base2_t::value;

      static constexpr camp::idx_t s_mask_per_register =
          (1<<log_base2_t::value)-1;


      static constexpr camp::idx_t s_minor_dim_elements =
          layout_type::is_row_major() ? s_num_columns : s_num_rows;

      // number of (full) registers that span the minor dim
      // if a single register is split across multiple rows or columns, then
      // this is 0
      static constexpr camp::idx_t s_minor_dim_registers =
              s_minor_dim_elements / s_elements_per_register;

      static_assert(s_minor_dim_registers >0  ||  log_base2_t::is_exact,
          "Minor dimension smaller than a vector need to be a power of two fraction");

      static_assert(s_minor_dim_registers == 0 || (s_minor_dim_elements % s_elements_per_register == 0),
          "Minor dimensions greater than a vector length must be an integer number of vectors");


      static constexpr camp::idx_t s_major_dim_per_register =
          s_elements_per_register / s_minor_dim_elements;

      static constexpr camp::idx_t s_segbits = RAJA::LogBase2<s_minor_dim_elements>::value;

    private:

      template<typename IDX>
      RAJA_INLINE
      RAJA_HOST_DEVICE
      constexpr
      static
      auto to_register(IDX row, IDX col) -> IDX {
        return layout_type::is_row_major() ?
            (row*IDX(COL_SIZE) + col) >> IDX(s_shift_per_register) :
            (col*IDX(ROW_SIZE) + row) >> IDX(s_shift_per_register);
      }

      template<typename IDX>
      RAJA_INLINE
      RAJA_HOST_DEVICE
      constexpr
      static
      auto to_lane(IDX row, IDX col) -> IDX {
        return layout_type::is_row_major() ?
            (row*IDX(COL_SIZE) + col) & IDX(s_mask_per_register) :
            (col*IDX(ROW_SIZE) + row) & IDX(s_mask_per_register);
      }

      using base_type::m_registers;

    public:


      RAJA_HOST_DEVICE
      RAJA_INLINE
      constexpr
      TensorRegister() : base_type() {}

      RAJA_HOST_DEVICE
      RAJA_INLINE
      TensorRegister(element_type c) : base_type(c)
      {
        this->broadcast(c);
      }


      RAJA_INLINE
      RAJA_HOST_DEVICE
      TensorRegister(self_type const &c) : base_type(c)
      {
        this->copy(c);
      }



      RAJA_HOST_DEVICE
      RAJA_INLINE
      ~TensorRegister(){}


      /*!
       * Returns true if the underlying data packed for a given tensor ref
       *
       * This is true if either:
       *   It's column major and the rows are stride one
       *   It's row major and the columns are stride one
       */
      template<camp::idx_t STRIDE_ONE_DIM>
      RAJA_HOST_DEVICE
      RAJA_INLINE
      static
      constexpr
      bool is_ref_packed() {
        return (STRIDE_ONE_DIM == 0 && layout_type::is_column_major()) ||
            (STRIDE_ONE_DIM == 1 && layout_type::is_row_major());
      }

      /*!
       * Gets the maximum size of matrix along specified dimension
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      static
      constexpr camp::idx_t s_dim_elem(camp::idx_t dim){
        return dim == 0 ? ROW_SIZE : COL_SIZE;
      }



      /*!
       * @brief Set entire vector to a single scalar value
       * @param value Value to set all vector elements to
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &operator=(element_type value)
      {
        this->broadcast(value);
        return *this;
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &operator=(self_type const &c){
        return this->copy(c);
      }


      /*!
       * Provide matrix-matrix multiply for operator* between to matrices
       */
      template<typename T2, typename L, typename RP>
      self_type
      operator*(SquareMatrixRegister<T2, L, RP> const &y) const
      {
        return matrix_multiply(y);
      }

      /*!
       * Provide right matrix-vector multiply for operator* between this
       * matrix and a vector.
       */
      template<typename T2, typename RP>
      VectorRegister<T2, RP>
      operator*(VectorRegister<T2, RP> const &y) const
      {
        return right_multiply_vector(y);
      }






      /*!
       * @brief Performs load specified by TensorRef object.
       */
      template<typename POINTER_TYPE, typename INDEX_TYPE, RAJA::internal::expt::TensorTileSize TENSOR_SIZE, camp::idx_t STRIDE_ONE_DIM>
      RAJA_INLINE
      RAJA_HOST_DEVICE
      self_type &load_ref(RAJA::internal::expt::TensorRef<POINTER_TYPE, INDEX_TYPE, TENSOR_SIZE, 2, STRIDE_ONE_DIM> const &ref){

        auto ptr = ref.m_pointer + ref.m_tile.m_begin[0]*ref.m_stride[0] +
                                   ref.m_tile.m_begin[1]*ref.m_stride[1];

        // check for packed data
        if(is_ref_packed<STRIDE_ONE_DIM>()){
          // full vector?
          if(TENSOR_SIZE == RAJA::internal::expt::TENSOR_FULL){
            load_packed(ptr, ref.m_stride[0], ref.m_stride[1]);
          }
          // partial
          else{
            load_packed_nm(ptr, ref.m_stride[0], ref.m_stride[1],
                                ref.m_tile.m_size[0], ref.m_tile.m_size[1]);
          }

        }
        // strided data
        else
        {
          // full vector?
          if(TENSOR_SIZE == RAJA::internal::expt::TENSOR_FULL){
            load_strided(ptr, ref.m_stride[0], ref.m_stride[1]);
          }
          // partial
          else{
            load_strided_nm(ptr, ref.m_stride[0], ref.m_stride[1],
                                ref.m_tile.m_size[0], ref.m_tile.m_size[1]);
          }
        }
        return *this;
      }


      /*!
       * @brief Performs load specified by TensorRef object.
       */
      template<typename POINTER_TYPE, typename INDEX_TYPE, RAJA::internal::expt::TensorTileSize TENSOR_SIZE, camp::idx_t STRIDE_ONE_DIM>
      RAJA_INLINE
      RAJA_HOST_DEVICE
      self_type const &store_ref(RAJA::internal::expt::TensorRef<POINTER_TYPE, INDEX_TYPE, TENSOR_SIZE,2, STRIDE_ONE_DIM> const &ref) const {

        auto ptr = ref.m_pointer + ref.m_tile.m_begin[0]*ref.m_stride[0] +
                                   ref.m_tile.m_begin[1]*ref.m_stride[1];

        // check for packed data
        if(is_ref_packed<STRIDE_ONE_DIM>())
        {
          // full vector?
          if(TENSOR_SIZE == RAJA::internal::expt::TENSOR_FULL){
            store_packed(ptr, ref.m_stride[0], ref.m_stride[1]);
          }
          // partial
          else{
            store_packed_nm(ptr, ref.m_stride[0], ref.m_stride[1],
                                ref.m_tile.m_size[0], ref.m_tile.m_size[1]);
          }

        }
        // strided data
        else
        {
          // full vector?
          if(TENSOR_SIZE == RAJA::internal::expt::TENSOR_FULL){
            store_strided(ptr, ref.m_stride[0], ref.m_stride[1]);
          }
          // partial
          else{
            store_strided_nm(ptr, ref.m_stride[0], ref.m_stride[1],
                                ref.m_tile.m_size[0], ref.m_tile.m_size[1]);
          }
        }
        return *this;
      }



      /*!
       * Loads a dense full matrix from memory.
       *
       * For row-major, column entries must be stride-1
       * For column-major, row entries must be stride-1
       *
       * Non-stride-1 dimension can have any striding... so this is can
       * be a "semi-dense" matrix.
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &load_packed(element_type const *ptr,
          int row_stride, int col_stride)
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: load_packed, stride=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, col_stride);
#endif
        // if it's dense in columns and rows, just do a dense load
        if((layout_type::is_row_major()&&(row_stride==COL_SIZE)) ||
           (layout_type::is_column_major()&&(col_stride==ROW_SIZE))){

          for(camp::idx_t reg = 0;reg < s_num_registers;++ reg){
            m_registers[reg].load_packed(ptr + reg*s_elements_per_register);
          }

        }
        // Do semi-dense load for row-major
        else if(layout_type::is_row_major()){

          // one or more registers per column
          if(s_minor_dim_registers){
            camp::idx_t reg = 0;
            for(camp::idx_t row = 0;row < ROW_SIZE;++ row){
              for(camp::idx_t colreg = 0;colreg < s_minor_dim_registers; ++ colreg){

                camp::idx_t offset = row*row_stride + colreg*s_elements_per_register;

                m_registers[reg].load_packed(ptr + offset);

                reg ++;

              }
            }
          }
          // more than one column per register
          else{
            // default to strided operation
            return load_strided(ptr, row_stride, col_stride);
          }
        }
        // Do semi-dense load for column-major
        else{
          // one or more registers per row
          if(s_minor_dim_registers){

            camp::idx_t reg = 0;
            for(camp::idx_t col = 0;col < COL_SIZE;++ col){
              for(camp::idx_t rowreg = 0;rowreg < s_minor_dim_registers; ++ rowreg){

                camp::idx_t offset = col*col_stride + rowreg*s_elements_per_register;

                m_registers[reg].load_packed(ptr + offset);

                reg ++;

              }
            }
          }
          // more than one column per register
          else{
            // default to strided operation
            return load_strided(ptr, row_stride, col_stride);
          }
        }

        return *this;
      }

      /*!
       * Loads a strided full matrix from memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &load_strided(element_type const *ptr,
          int row_stride, int col_stride)
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: load_strided, stride=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, col_stride);
#endif

        if(layout_type::is_row_major()){
          // one or more registers per row
          if(s_minor_dim_registers){
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t row = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              camp::idx_t col = s_elements_per_register * (i - (row*s_minor_dim_registers));
              m_registers[i].load_strided(ptr+row*row_stride+col*col_stride, col_stride);
            }
          }
          // less than one register per row
          else
          {
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              element_type const *ptr_i = ptr + i * row_stride*s_major_dim_per_register;
              m_registers[i].segmented_load(ptr_i, s_segbits, col_stride, row_stride);
            }
          }
        }

        // column major
        else{

          // one or more registers per column
          if(s_minor_dim_registers){
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t col = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              camp::idx_t row = s_elements_per_register * (i - (col*s_minor_dim_registers));

              m_registers[i].load_strided(ptr+row*row_stride+col*col_stride, row_stride);
            }
          }
          // less than one register per column
          else
          {
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              element_type const *ptr_i = ptr + i * col_stride*s_major_dim_per_register;
              m_registers[i].segmented_load(ptr_i, s_segbits, row_stride, col_stride);
            }
          }
        }

        return *this;
      }

      /*!
       * Loads a dense partial matrix from memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &load_packed_nm(element_type const *ptr,
          int row_stride, int col_stride,
          int num_rows, int num_cols)
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: load_packed_nm, stride=%d,%d, nm=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, 1, num_rows, num_cols);
#endif

        if(layout_type::is_row_major()){

          // one or more registers per column
          if(s_minor_dim_registers){

            for(camp::idx_t row = 0;row < num_rows;++ row){
              for(camp::idx_t colreg = 0;colreg < s_minor_dim_registers; ++ colreg){

                camp::idx_t reg = row*s_minor_dim_registers + colreg;

                camp::idx_t col0 = colreg*s_elements_per_register;
                camp::idx_t offset = row*row_stride + col0;

                // loading a complete register
                if(col0+s_elements_per_register <= num_cols){
                  m_registers[reg].load_packed(ptr + offset);
                }

                // partial register at end of row
                else{
                  m_registers[reg].load_packed_n(ptr + offset, num_cols - col0);

                  // zero out the remaining registers, if any
                  for(camp::idx_t i = colreg+1;i < s_minor_dim_registers;++i){
                    reg++;
                    m_registers[reg] = element_type(0);
                  }

                  break; // end this row
                }
              }
            }

            // zero out remaining rows
            for(camp::idx_t row = num_rows;row < ROW_SIZE;++ row){
              for(camp::idx_t colreg = 0;colreg < s_minor_dim_registers; ++ colreg){

                camp::idx_t reg = row*s_minor_dim_registers + colreg;

                m_registers[reg] = element_type(0);
              }
            }
          }
          // more than one column per register
          else{
            // default to strided operation
            return load_strided_nm(ptr, row_stride, col_stride, num_rows, num_cols);
          }
        }
        // Do semi-dense load for column-major
        else{

          // one or more registers per column
          if(s_minor_dim_registers){

            for(camp::idx_t col = 0;col < num_cols;++ col){
              for(camp::idx_t rowreg = 0;rowreg < s_minor_dim_registers; ++ rowreg){

                camp::idx_t reg = col*s_minor_dim_registers + rowreg;

                camp::idx_t row0 = rowreg*s_elements_per_register;
                camp::idx_t offset = col*col_stride + row0;

                // loading a complete register
                if(row0+s_elements_per_register <= num_rows){
                  m_registers[reg].load_packed(ptr + offset);
                }

                // partial register at end of column
                else{
                  m_registers[reg].load_packed_n(ptr + offset, num_rows - row0);

                  // zero out the remaining registers, if any
                  for(camp::idx_t i = rowreg+1;i < s_minor_dim_registers;++i){
                    reg++;
                    m_registers[reg] = element_type(0);
                  }

                  break; // end this column
                }
              }
            }
            // zero out remaining columns
            for(camp::idx_t col = num_cols;col < COL_SIZE;++ col){
              for(camp::idx_t rowreg = 0;rowreg < s_minor_dim_registers; ++ rowreg){

                camp::idx_t reg = col*s_minor_dim_registers + rowreg;

                m_registers[reg] = element_type(0);
              }
            }

          }
          // more than one column per register
          else{

            // default to strided operation
            return load_strided_nm(ptr, row_stride, col_stride, num_rows, num_cols);
          }
        }

        return *this;
      }

      /*!
       * Loads a strided partial matrix from memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &load_strided_nm(element_type const *ptr,
          int row_stride, int col_stride,
          int num_rows, int num_cols)
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: load_strided_nm, stride=%d,%d, nm=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, col_stride, num_rows, num_cols);
#endif

        if(layout_type::is_row_major()){
          // one or more registers per row
          if(s_minor_dim_registers){

            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t row = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              if(row >= num_rows){
                m_registers[i] = element_type(0);
              }
              else{
                camp::idx_t col = s_elements_per_register * (i - (row*s_minor_dim_registers));


                camp::idx_t reg_num_cols = s_elements_per_register;
                if(reg_num_cols+col > num_cols){
                  reg_num_cols = num_cols-col;
                  m_registers[i].load_strided_n(ptr+row*row_stride+col*col_stride, col_stride, reg_num_cols);
                }
                else{
                  m_registers[i].load_strided(ptr+row*row_stride+col*col_stride, col_stride);
                }


              }
            }
          }
          // less than one register per row
          else
          {

            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              // figure out how many rows get loaded in this register
              camp::idx_t reg_num_rows = num_rows - i*s_major_dim_per_register;
              reg_num_rows = reg_num_rows > s_major_dim_per_register ? s_major_dim_per_register : reg_num_rows;

              element_type const *ptr_i = ptr + i * row_stride*s_major_dim_per_register;
              m_registers[i].segmented_load_nm(ptr_i, s_segbits, col_stride, row_stride, num_cols, reg_num_rows);
            }
          }
        }

        // column major
        else{

          // one or more registers per column
          if(s_minor_dim_registers){
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t col = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              if(col >= num_cols){
                m_registers[i] = element_type(0);
              }
              else{
                camp::idx_t row = s_elements_per_register * (i - (col*s_minor_dim_registers));

                camp::idx_t reg_num_rows = s_elements_per_register;
                if(reg_num_rows+row > num_rows){
                  reg_num_rows = num_rows-row;
                  m_registers[i].load_strided_n(ptr+row*row_stride+col*col_stride, row_stride, reg_num_rows);
                }
                else{
                  m_registers[i].load_strided(ptr+row*row_stride+col*col_stride, row_stride);
                }
              }
            }
          }
          // less than one register per column
          else
          {
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              // figure out how many columns get loaded in this register
              camp::idx_t reg_num_cols = num_cols - i*s_major_dim_per_register;
              reg_num_cols = reg_num_cols > s_major_dim_per_register ? s_major_dim_per_register : reg_num_cols;

              element_type const *ptr_i = ptr + i * col_stride*s_major_dim_per_register;
              m_registers[i].segmented_load_nm(ptr_i, s_segbits, row_stride, col_stride, num_rows, reg_num_cols);
            }
          }
        }

        return *this;
      }



      /*!
       * Store a dense full matrix to memory.
       *
       * Column entries must be stride-1, rows may be any striding
       */
      RAJA_SUPPRESS_HD_WARN
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type const &store_packed(element_type *ptr,
          int row_stride, int col_stride) const
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: store_packed, stride=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, 1);
#endif

        // if it's dense in columns and rows, just do a dense load
        if((layout_type::is_row_major()&&(row_stride==COL_SIZE)) ||
           (layout_type::is_column_major()&&(col_stride==ROW_SIZE))){

          for(camp::idx_t reg = 0;reg < s_num_registers;++ reg){
            m_registers[reg].store_packed(ptr + reg*s_elements_per_register);
          }

        }
        // Do semi-dense store for row-major
        else if(layout_type::is_row_major()){

          // one or more registers per column
          if(s_minor_dim_registers){
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t row = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              camp::idx_t col = s_elements_per_register * (i - (row*s_minor_dim_registers));
              m_registers[i].store_packed(ptr+row*row_stride+col*col_stride);
            }
          }
          // more than one column per register
          else{
            store_strided(ptr, row_stride, col_stride);
          }
        }
        // Do semi-dense store for column-major
        else{
          // one or more registers per row
          if(s_minor_dim_registers){
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t col = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              camp::idx_t row = s_elements_per_register * (i - (col*s_minor_dim_registers));
              m_registers[i].store_packed(ptr+row*row_stride+col*col_stride);
            }
          }
          // more than one row per register
          else{
            store_strided(ptr, row_stride, col_stride);
          }
        }


        return *this;
      }

      /*!
       * Store a strided full matrix to memory
       */
      RAJA_SUPPRESS_HD_WARN
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type const &store_strided(element_type *ptr,
          int row_stride, int col_stride) const
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: store_strided, stride=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, col_stride);
#endif


        if(layout_type::is_row_major()){
          // one or more registers per row
          if(s_minor_dim_registers){
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t row = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              camp::idx_t col = s_elements_per_register * (i - (row*s_minor_dim_registers));
              m_registers[i].store_strided(ptr+row*row_stride+col*col_stride, col_stride);
            }
          }
          // less than one register per row
          else
          {
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              element_type *ptr_i = ptr + i * row_stride*s_major_dim_per_register;
              m_registers[i].segmented_store(ptr_i, s_segbits, col_stride, row_stride);
            }
          }
        }

        // column major
        else{
          // one or more registers per column
          if(s_minor_dim_registers){
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t col = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              camp::idx_t row = s_elements_per_register * (i - (col*s_minor_dim_registers));
              m_registers[i].store_strided(ptr+row*row_stride+col*col_stride, row_stride);
            }
          }
          // less than one register per column
          else
          {
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              element_type *ptr_i = ptr + i * col_stride*s_major_dim_per_register;
              m_registers[i].segmented_store(ptr_i, s_segbits, row_stride, col_stride);
            }
          }
        }

        return *this;
      }

      /*!
       * Store a dense partial matrix to memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type const &store_packed_nm(element_type *ptr,
          int row_stride, int col_stride,
          int num_rows, int num_cols) const
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: RM store_packed_nm, stride=%d,%d, nm=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, 1, num_rows, num_cols);
#endif


        if(layout_type::is_row_major()){

          // one or more registers per column
          if(s_minor_dim_registers){

            for(camp::idx_t row = 0;row < num_rows;++ row){
              for(camp::idx_t colreg = 0;colreg < s_minor_dim_registers; ++ colreg){

                camp::idx_t reg = row*s_minor_dim_registers + colreg;

                camp::idx_t col0 = colreg*s_elements_per_register;
                camp::idx_t offset = row*row_stride + col0;

                // store a complete register
                if(col0+s_elements_per_register <= num_cols){
                  m_registers[reg].store_packed(ptr + offset);
                }

                // partial register at end of row
                else{
                  m_registers[reg].store_packed_n(ptr + offset, num_cols - col0);

                  break; // end this row
                }
              }
            }

          }
          // more than one column per register
          else{
            // default to strided operation
            return store_strided_nm(ptr, row_stride, col_stride, num_rows, num_cols);
          }
        }
        // Do semi-dense store for column-major
        else{

          // one or more registers per column
          if(s_minor_dim_registers){

            for(camp::idx_t col = 0;col < num_cols;++ col){
              for(camp::idx_t rowreg = 0;rowreg < s_minor_dim_registers; ++ rowreg){

                camp::idx_t reg = col*s_minor_dim_registers + rowreg;

                camp::idx_t row0 = rowreg*s_elements_per_register;
                camp::idx_t offset = col*col_stride + row0;

                // loading a complete register
                if(row0+s_elements_per_register <= num_rows){
                  m_registers[reg].store_packed(ptr + offset);
                }

                // partial register at end of column
                else{
                  m_registers[reg].store_packed_n(ptr + offset, num_rows - row0);

                  break; // end this column
                }
              }
            }

          }
          // more than one column per register
          else{

            // default to strided operation
            return store_strided_nm(ptr, row_stride, col_stride, num_rows, num_cols);
          }
        }

        return *this;
      }

      /*!
       * Store a strided partial matrix to memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type const &store_strided_nm(element_type *ptr,
          int row_stride, int col_stride,
          int num_rows, int num_cols) const
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: RM store_strided_nm, stride=%d,%d, nm=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, col_stride, num_rows, num_cols);
#endif


        if(layout_type::is_row_major()){
          // one or more registers per row
          if(s_minor_dim_registers){

            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t row = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              if(row < num_rows){
                camp::idx_t col = s_elements_per_register * (i - (row*s_minor_dim_registers));


                camp::idx_t reg_num_cols = s_elements_per_register;
                if(reg_num_cols+col > num_cols){
                  reg_num_cols = num_cols-col;
                  m_registers[i].store_strided_n(ptr+row*row_stride+col*col_stride, col_stride, reg_num_cols);
                }
                else{
                  m_registers[i].store_strided(ptr+row*row_stride+col*col_stride, col_stride);
                }


              }
            }
          }
          // less than one register per row
          else
          {

            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              // figure out how many rows get loaded in this register
              camp::idx_t reg_num_rows = num_rows - i*s_major_dim_per_register;
              reg_num_rows = reg_num_rows > s_major_dim_per_register ? s_major_dim_per_register : reg_num_rows;

              element_type *ptr_i = ptr + i * row_stride*s_major_dim_per_register;
              m_registers[i].segmented_store_nm(ptr_i, s_segbits, col_stride, row_stride, num_cols, reg_num_rows);
            }
          }
        }

        // column major
        else{

          // one or more registers per column
          if(s_minor_dim_registers){
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t col = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              if(col < num_cols){
                camp::idx_t row = s_elements_per_register * (i - (col*s_minor_dim_registers));

                camp::idx_t reg_num_rows = s_elements_per_register;
                if(reg_num_rows+row > num_rows){
                  reg_num_rows = num_rows-row;
                  m_registers[i].store_strided_n(ptr+row*row_stride+col*col_stride, row_stride, reg_num_rows);
                }
                else{
                  m_registers[i].store_strided(ptr+row*row_stride+col*col_stride, row_stride);
                }
              }
            }
          }
          // less than one register per column
          else
          {
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              // figure out how many columns get loaded in this register
              camp::idx_t reg_num_cols = num_cols - i*s_major_dim_per_register;
              reg_num_cols = reg_num_cols > s_major_dim_per_register ? s_major_dim_per_register : reg_num_cols;

              element_type *ptr_i = ptr + i * col_stride*s_major_dim_per_register;
              m_registers[i].segmented_store_nm(ptr_i, s_segbits, row_stride, col_stride, num_rows, reg_num_cols);
            }
          }
        }

        return *this;
      }


      RAJA_SUPPRESS_HD_WARN
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type divide_nm(self_type const &mat, int num_rows, int num_cols) const {
        self_type result;
//        for(camp::idx_t reg = 0;reg < s_num_registers;++ reg){
//          result.vec(reg) = m_registers[reg].divide(mat.vec(reg));
//        }

        if(layout_type::is_row_major()){
          // one or more registers per row
          if(s_minor_dim_registers){

            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t row = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              if(row < num_rows){
                camp::idx_t col = s_elements_per_register * (i - (row*s_minor_dim_registers));


                camp::idx_t reg_num_cols = s_elements_per_register;
                if(reg_num_cols+col > num_cols){
                  reg_num_cols = num_cols-col;
                  result.m_registers[i] = m_registers[i].divide_n(mat.m_registers[i], reg_num_cols);
                }
                else{
                  result.m_registers[i] = m_registers[i].divide(mat.m_registers[i]);
                }


              }
            }
          }
          // less than one register per row
          else
          {

            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              // figure out how many rows get loaded in this register
              camp::idx_t reg_num_rows = num_rows - i*s_major_dim_per_register;
              reg_num_rows = reg_num_rows > s_major_dim_per_register ? s_major_dim_per_register : reg_num_rows;

              result.m_registers[i] = m_registers[i].segmented_divide_nm(mat.m_registers[i], s_segbits, num_cols, reg_num_rows);
            }
          }
        }

        // column major
        else{

          // one or more registers per column
          if(s_minor_dim_registers){
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              camp::idx_t col = i / (s_minor_dim_registers ? s_minor_dim_registers : 1);
              if(col < num_cols){
                camp::idx_t row = s_elements_per_register * (i - (col*s_minor_dim_registers));

                camp::idx_t reg_num_rows = s_elements_per_register;
                if(reg_num_rows+row > num_rows){
                  reg_num_rows = num_rows-row;
                  result.m_registers[i] = m_registers[i].divide_n(mat.m_registers[i], reg_num_rows);
                }
                else{
                  result.m_registers[i] = m_registers[i].divide(mat.m_registers[i]);
                }
              }
            }
          }
          // less than one register per column
          else
          {
            for(camp::idx_t i = 0;i < s_num_registers;++ i){
              // figure out how many columns get loaded in this register
              camp::idx_t reg_num_cols = num_cols - i*s_major_dim_per_register;
              reg_num_cols = reg_num_cols > s_major_dim_per_register ? s_major_dim_per_register : reg_num_cols;

              result.m_registers[i] = m_registers[i].segmented_divide_nm(mat.m_registers[i], s_segbits, num_rows, reg_num_cols);
            }
          }
        }


        return result;
      }



      /*!
       * Matrix transpose, keeping layout
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type transpose() const {

        static constexpr camp::idx_t num_elem = register_type::s_num_elem;

        /*
         * We use Eklundh's Algorithm: Recursive block transpose because
         * it's easy to implement using SIMD register permutation primitives
         *
         * Executes in n*log(n) row operations
         *
         */
        self_type result = *this;
        for(camp::idx_t lvl = 0; (1<<lvl) < num_elem;++ lvl){
          // At this level, we do block transposes of NxN sub-matrices, where
          // N = 1<<lvl

          auto const &vals = result.m_registers;

          self_type tmp;
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            if(((i>>lvl)&0x1) == 0){
              tmp.m_registers[i] = vals[i - (i&(1<<lvl))].transpose_shuffle_left(lvl, vals[i - (i&(1<<lvl)) + (1<<lvl)]);
            }
            else{
              tmp.m_registers[i] = vals[i - (i&(1<<lvl))].transpose_shuffle_right(lvl, vals[i - (i&(1<<lvl)) + (1<<lvl)]);
            }
          }
          result = tmp;
        }

        return result;

      }


      /*!
       * Matrix transpose inplace
       *
       * Modifies contents of this matrix
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      void inplace_transpose() {
        *this = transpose();
      }

      /*!
       * Transpose this matrix by swapping row/column majorness
       *
       * Row major matrix returns column major, and visa versa.
       *
       * This has zero cost.
       *
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      transpose_tensor_type const &transpose_by_type() const {
        return reinterpret_cast<transpose_tensor_type const &>(*this);
      }

      /*!
       * Matrix vector product
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      column_vector_type right_multiply_vector(row_vector_type v) const {
        column_vector_type result(0);
        return right_multiply_vector_accumulate(v, result);
      }

      /*!
       * Matrix vector product
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      row_vector_type left_multiply_vector(column_vector_type v) const {
        row_vector_type result(0);
        return left_multiply_vector_accumulate(v, result);
      }


      /*!
       * Matrix vector product with accumulation into another vector
       *
       * acc += (this) * v
       */
      RAJA_SUPPRESS_HD_WARN
      RAJA_HOST_DEVICE
      RAJA_INLINE
      column_vector_type right_multiply_vector_accumulate(row_vector_type const &v, column_vector_type result) const {

        if(layout_type::is_row_major()){

          // 1 register is split over multiple rows
          if(s_minor_dim_registers == 0){
//            printf("A\n");


            // start by broadcasting the first segment in v across all of v
            // we will use this term for all registers in the matrix
            auto vv = v.get_register(0).segmented_broadcast_inner(s_segbits, 0);

            // loop over output segments, which is also the number of
            // registers in the matrix (no kidding!)
            RAJA_UNROLL
            for(camp::idx_t outseg = 0;outseg < s_num_registers;++ outseg){

              // compute which result register we are accumulating into
              camp::idx_t result_reg = outseg >> s_segbits;

              // compute which segment within result_reg we are accumulating into
              camp::idx_t result_seg = outseg - (result_reg<<s_segbits);

              // compute segmented dot product to get output segment
              auto value = m_registers[outseg].segmented_dot(s_segbits, result_seg, vv);

              // accumulate result
              result.get_register(result_reg) += value;
            }

          }
          // one or more registers per row
          else{
//            printf("B\n");

            // Loop over rows
            camp::idx_t reg = 0;
            RAJA_UNROLL
            for(camp::idx_t row = 0;row < s_num_rows;++ row){

              // compute partial dot products for all registers in this row
              auto rowsum = register_type(0);
              RAJA_UNROLL
              for(camp::idx_t colreg = 0;colreg < s_minor_dim_registers;++ colreg){

                rowsum = m_registers[reg].multiply_add(v.get_register(colreg), rowsum);
                reg ++;

              } // rowreg

              // finish dot product by taking sum of rowsum
              auto value = result.get(row) + rowsum.sum();
              result.set(value, row);

            } // row
          }

        }
        else{


          // 1 register is split over multiple columns
          if(s_minor_dim_registers == 0){
//            printf("C\n");
            auto &mv = result.get_register(0);

            // Loop over registers, which are also the segments in v
            RAJA_UNROLL
            for(camp::idx_t m_reg = 0;m_reg < s_num_registers;++ m_reg){
              camp::idx_t v_reg = m_reg >> s_segbits;
              camp::idx_t v_seg = m_reg & ( (1<<s_segbits) - 1);

              auto v_tmp = v.get_register(v_reg).segmented_broadcast_outer(s_segbits, v_seg);
              mv = m_registers[m_reg].multiply_add(v_tmp, mv);

            }

            // Now sum segments in mv together to form final result
            mv = mv.segmented_sum_outer(s_segbits, 0);

          }
          // one or more registers per column
          else{
//            printf("D\n");

            // Loop over columns (which is also registers)
            camp::idx_t reg = 0;
            RAJA_UNROLL
            for(camp::idx_t col = 0;col < s_num_columns;++ col){

              // extract column value from v
              auto v_col = register_type(v.get(col));

              // apply v_col to entire column (1 or more registers)
              RAJA_UNROLL
              for(camp::idx_t rowreg = 0;rowreg < s_minor_dim_registers;++ rowreg){

                auto &mv = result.get_register(rowreg);
                mv = m_registers[reg].multiply_add(v_col, mv);

                reg ++;

              } // rowreg
            } // col
          }

        }
        return result;
      }

      /*!
       * Matrix vector product with accumulation into another vector
       *
       * acc += v * (this)
       */
      RAJA_SUPPRESS_HD_WARN
      RAJA_HOST_DEVICE
      RAJA_INLINE
      row_vector_type left_multiply_vector_accumulate(column_vector_type const &v, row_vector_type result) const {

        if(layout_type::is_row_major()){
//          printf("Aleft\n");

          // 1 register is split over multiple columns
          if(s_minor_dim_registers == 0){
            auto &vm = result.get_register(0);

            // Loop over registers, which are also the segments in v
            RAJA_UNROLL
            for(camp::idx_t m_reg = 0;m_reg < s_num_registers;++ m_reg){
              camp::idx_t v_reg = m_reg >> s_segbits;
              camp::idx_t v_seg = m_reg & ( (1<<s_segbits) - 1);

              auto v_tmp = v.get_register(v_reg).segmented_broadcast_outer(s_segbits, v_seg);
              vm = m_registers[m_reg].multiply_add(v_tmp, vm);

            }

            // Now sum segments in mv together to form final result
            vm = vm.segmented_sum_outer(s_segbits, 0);

          }
          // one or more registers per row
          else{
//            printf("Bleft\n");
            // Loop over rows
            camp::idx_t reg = 0;
            RAJA_UNROLL
            for(camp::idx_t row = 0;row < s_num_rows;++ row){
              auto lhs_bcat = register_type(v.get(row));
              RAJA_UNROLL
              for(camp::idx_t colreg = 0;colreg < s_minor_dim_registers;++ colreg){

                result.get_register(colreg) =
                    m_registers[reg].multiply_add(lhs_bcat, result.get_register(colreg));
                reg ++;

              } // rowreg

            }

          }


        } // row-major

        // Column-major:
        else{
          // 1 register is split over multiple rows
          if(s_minor_dim_registers == 0){
//            printf("Cleft\n");

            // start by broadcasting the first segment in v across all of v
            // we will use this term for all registers in the matrix
            auto vv = v.get_register(0).segmented_broadcast_inner(s_segbits, 0);

            // loop over output segments, which is also the number of
            // registers in the matrix (no kidding!)
            RAJA_UNROLL
            for(camp::idx_t outseg = 0;outseg < s_num_registers;++ outseg){

              // compute which result register we are accumulating into
              camp::idx_t result_reg = outseg >> s_segbits;

              // compute which segment within result_reg we are accumulating into
              camp::idx_t result_seg = outseg - (result_reg<<s_segbits);

              // compute segmented dot product to get output segment
              auto value = m_registers[outseg].segmented_dot(s_segbits, result_seg, vv);

              // accumulate result
              result.get_register(result_reg) += value;
            }

          }
          // one or more registers per column
          else{
//            printf("Dleft\n");
            // Loop over rows
            camp::idx_t reg = 0;
            RAJA_UNROLL
            for(camp::idx_t col = 0;col < s_num_columns;++ col){

              // compute partial dot products for all registers in this row
              auto colsum = register_type(0);
              RAJA_UNROLL
              for(camp::idx_t rowreg = 0;rowreg < s_minor_dim_registers;++ rowreg){
                colsum = m_registers[reg].multiply_add(v.get_register(rowreg), colsum);
                reg ++;

              } // rowreg

              // finish dot product by taking sum of rowsum
              auto value = result.get(col) + colsum.sum();
              result.set(value, col);

            } // col
          }


        } // col-major
        return result;
      }





      /*!
       * Matrix-Matrix product
       */
      template<typename RMAT>
      RAJA_HOST_DEVICE
      RAJA_INLINE
      typename RAJA::internal::expt::MatrixMatrixMultiplyHelper<self_type, RMAT>::result_type
      matrix_multiply(RMAT const &mat) const {
#ifdef __CUDA_ARCH__
        if(threadIdx.x==0){
        printf("matrix_multiply\n");
        }
#endif
        typename RAJA::internal::expt::MatrixMatrixMultiplyHelper<self_type, RMAT>::result_type res(0);
        RAJA::internal::expt::MatrixMatrixMultiplyHelper<self_type,RMAT>::multiply(*this, mat, res);
        return res;
      }

      /*!
       * Matrix-Matrix multiply add
       */
      template<typename RMAT>
      RAJA_HOST_DEVICE
      RAJA_INLINE
      typename RAJA::internal::expt::MatrixMatrixMultiplyHelper<self_type, RMAT>::result_type
      matrix_multiply_add(RMAT const &B, typename RAJA::internal::expt::MatrixMatrixMultiplyHelper<self_type, RMAT>::result_type const &C) const {
//#ifdef __CUDA_ARCH__
//        if(threadIdx.x==0){
//        printf("matrix_multiply_add\n");
//        }
//#endif
        typename RAJA::internal::expt::MatrixMatrixMultiplyHelper<self_type, RMAT>::result_type res(C);
        RAJA::internal::expt::MatrixMatrixMultiplyHelper<self_type,RMAT>::multiply_accumulate(*this, B, res);
        return res;
      }

      /*!
       * Matrix-Matrix multiply accumulate
       */
      template<typename ACCMAT, typename RMAT>
      RAJA_HOST_DEVICE
      RAJA_INLINE
      void
      matrix_multiply_accumulate(ACCMAT &acc, RMAT const &B) const {
//#ifdef __CUDA_ARCH__
//        if(threadIdx.x==0){
//        printf("matrix_multiply_accumulate\n");
//        }
//#endif
        RAJA::internal::expt::MatrixMatrixMultiplyHelper<self_type,RMAT>::multiply_accumulate(*this, B, acc);
      }



      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &set(element_type val, int row, int col){
        m_registers[to_register(row, col)].set(val, to_lane(row,col));
        return *this;
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      element_type get(int row, int col) const {
        return m_registers[to_register(row, col)].get(to_lane(row,col));
      }


      RAJA_HOST_DEVICE
      RAJA_INLINE
      register_type extract_diagonal_register(camp::idx_t starting_column, camp::idx_t segbits, camp::idx_t segment) const {

        register_type result(0);

        camp::idx_t num_rows = register_type::s_num_elem >> segbits;
        camp::idx_t num_repeats = 1 << segbits;

        camp::idx_t col0 = (starting_column + num_rows*segment)%s_num_columns;
        camp::idx_t row0 = num_rows*segment;

        for(camp::idx_t i = 0;i < num_rows;++i){
          camp::idx_t col = (col0 + i) % s_num_columns;
          camp::idx_t row = row0 + i;
          auto value = get(row,col);
          for(camp::idx_t j = 0;j < num_repeats;++j){
            result.set(value, (i<<segbits) + j);
          }
        }

        return result;
      }


      /*!
       * @brief Converts to matrix to a string
       *
       *
       */
      RAJA_INLINE
      std::string to_string(bool one_line=false) const {
        std::string s = "Matrix(" + std::to_string(s_num_rows) +
            "x" + std::to_string(s_num_columns);
        if(!one_line){
          s +=")\n";
        }


        s += "[ ";

        //
        for(camp::idx_t r = 0;r < s_num_rows; ++ r){
          if(r > 0){
            s += ", ";
            if(!one_line){
              s+= "\n  ";
            }
          }
          s += "[";
          for(camp::idx_t c = 0;c < s_num_columns; ++ c){
            if(c > 0){
              s += ", ";
            }
            s += std::to_string(this->get(r,c));
          }
          s += "]";
        }

        s += " ]";
        if(!one_line){
          s+="\n";
        }
        return s;
      }

  }; // MatrixRegisterImpl





} // namespace expt
}  // namespace RAJA




#endif
