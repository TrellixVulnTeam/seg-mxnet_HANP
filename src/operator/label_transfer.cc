#include "./label_transfer-inl.h"
#include "mshadow/tensor.h"   
#include <dmlc/logging.h>     
#include <dmlc/parameter.h>
#include <mxnet/operator.h>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <utility>
#include <math.h>
#include <functional>

namespace mshadow {
  template<typename DType>
    inline void Transfer(Tensor<cpu, 2, DType> dst, const Tensor<cpu, 2, DType> &src, const Tensor<cpu, 1 ,DType> ids, const DType ignore_label) {
      #pragma omp parallel for
      for (openmp_index_t y = 0; y < dst.size(0); ++y) {
        for (index_t x = 0; x < dst.size(1); ++x) {
          const index_t k = static_cast<int>(src[y][x]);
          index_t dst_id = static_cast<int>(ignore_label);
          if (k < static_cast<int>(ids.size(0))){
            dst_id = static_cast<int>(ids[k]);
          }
          dst[y][x] = static_cast<int>(dst_id);
        }
      }
    }
} // namespace mshadow

namespace mxnet {
namespace op {
template<>
Operator *CreateOp<cpu>(LabelTransferParam param, int dtype) {
  Operator *op = NULL;
  MSHADOW_REAL_TYPE_SWITCH(dtype, DType, {
    op = new LabelTransferOp<cpu, DType>(param);
  })
  return op;
}

// DO_BIND_DISPATCH comes from operator_common.h
Operator *LabelTransferProp::CreateOperatorEx(Context ctx, std::vector<TShape> *in_shape,
                                     std::vector<int> *in_type) const {
  DO_BIND_DISPATCH(CreateOp, param_, (*in_type)[0]);
}

DMLC_REGISTER_PARAMETER(LabelTransferParam);

MXNET_REGISTER_OP_PROPERTY(LabelTransfer, LabelTransferProp)
.describe(R"code(Computes the gradient of cross entropy loss with respect to softmax output.

- This operator computes the gradient in two steps.
  The cross entropy loss does not actually need to be computed.

  - Applies softmax function on the input array.
  - Computes and returns the gradient of cross entropy loss w.r.t. the softmax output.

- The softmax function, cross entropy loss and gradient is given by:

  - Softmax Function:

    .. math:: \text{softmax}(x)_i = \frac{exp(x_i)}{\sum_j exp(x_j)}

  - Cross Entropy Function:

    .. math:: \text{CE(label, output)} = - \sum_i \text{label}_i \log(\text{output}_i)

  - The gradient of cross entropy loss w.r.t softmax output:

    .. math:: \text{gradient} = \text{output} - \text{label}

- During forward propagation, the softmax function is computed for each instance in the input array.

  For general *N*-D input arrays with shape :math:`(d_1, d_2, ..., d_n)`. The size is
  :math:`s=d_1 \cdot d_2 \cdot \cdot \cdot d_n`. We can use the parameters `preserve_shape`
  and `multi_output` to specify the way to compute softmax:

  - By default, `preserve_shape` is ``false``. This operator will reshape the input array
    into a 2-D array with shape :math:`(d_1, \frac{s}{d_1})` and then compute the softmax function for
    each row in the reshaped array, and afterwards reshape it back to the original shape
    :math:`(d_1, d_2, ..., d_n)`.
  - If `preserve_shape` is ``true``, the softmax function will be computed along
    the last axis (`axis` = ``-1``).
  - If `multi_output` is ``true``, the softmax function will be computed along
    the second axis (`axis` = ``1``).

- During backward propagation, the gradient of cross-entropy loss w.r.t softmax output array is computed.
  The provided label can be a one-hot label array or a probability label array.

  - If the parameter `use_ignore` is ``true``, `ignore_label` can specify input instances
    with a particular label to be ignored during backward propagation. **This has no effect when
    softmax `output` has same shape as `label`**.

    Example::

      data = [[1,2,3,4],[2,2,2,2],[3,3,3,3],[4,4,4,4]]
      label = [1,0,2,3]
      ignore_label = 1
      SoftmaxOutput(data=data, label = label,\
                    multi_output=true, use_ignore=true,\
                    ignore_label=ignore_label)
      ## forward softmax output
      [[ 0.0320586   0.08714432  0.23688284  0.64391428]
       [ 0.25        0.25        0.25        0.25      ]
       [ 0.25        0.25        0.25        0.25      ]
       [ 0.25        0.25        0.25        0.25      ]]
      ## backward gradient output
      [[ 0.    0.    0.    0.  ]
       [-0.75  0.25  0.25  0.25]
       [ 0.25  0.25 -0.75  0.25]
       [ 0.25  0.25  0.25 -0.75]]
      ## notice that the first row is all 0 because label[0] is 1, which is equal to ignore_label.

  - The parameter `grad_scale` can be used to rescale the gradient, which is often used to
    give each loss function different weights.

  - This operator also supports various ways to normalize the gradient by `normalization`,
    The `normalization` is applied if softmax output has different shape than the labels.
    The `normalization` mode can be set to the followings:

    - ``'null'``: do nothing.
    - ``'batch'``: divide the gradient by the batch size.
    - ``'valid'``: divide the gradient by the number of instances which are not ignored.

)code" ADD_FILELINE)
.add_argument("label", "NDArray-or-Symbol", "Ground truth label.")
.add_arguments(LabelTransferParam::__FIELDS__());


}  // namespace op
}  // namespace mxnet
