#include "./ohemsoftmax_output-inl.h"
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
inline void OhemSoftmaxGrad(Tensor<cpu, 3, DType> dst,
                            const Tensor<cpu, 3, DType> &src,
                            const Tensor<cpu, 2, DType> &label,
                            const DType &ignore_label,
                            const DType &threshold,
                            const DType &margin) {
#pragma omp parallel for
  for (openmp_index_t n = 0; n < dst.size(2); ++n) {
    for (index_t y = 0; y < dst.size(0); ++y) {
      const int k = static_cast<int>(label[y][n]);
      // Ignore label
      if (k == static_cast<int>(ignore_label)) {
        for (int x = 0; x < static_cast<int>(dst.size(1)); ++x) {
          dst[y][x][n] = DType(0.0f);
        }
        continue;
      }
      // Ohem judge
      DType predict_score = src[y][k][n];
      if (threshold > 0.0f && predict_score > threshold) {
        for (int x = 0; x < static_cast<int>(dst.size(1)); ++x) {
          dst[y][x][n] = DType(0.0f);
        }
        continue;
      }
      // Margin judge
      if (margin > 0.0f && static_cast<int>(src.size(1)) > 2) {
        std::vector<DType> distri;
        for (int x = 0; x < static_cast<int>(src.size(1)); ++x) {
          distri.push_back(src[y][x][n]);
        }
        std::sort(distri.begin(), distri.end(), std::greater<DType>());
        if (predict_score - distri[1] > margin) {
          for (int x = 0; x < static_cast<int>(dst.size(1)); ++x) {
            dst[y][x][n] = DType(0.0f);
          }
          continue;
        }
      }
      // Softmax gradient
      for (int x = 0; x < static_cast<int>(dst.size(1)); ++x) {
        if (x == k) {
          dst[y][k][n] = src[y][k][n] - 1.0f;
        } else {
          dst[y][x][n] = src[y][x][n];
        }
      }
    }
  }
}

} // namespace mshadow

namespace mxnet {
namespace op {
template<>
Operator *CreateOp<cpu>(OhemSoftmaxOutputParam param, int dtype) {
  Operator *op = NULL;
  MSHADOW_REAL_TYPE_SWITCH(dtype, DType, {
    op = new OhemSoftmaxOutputOp<cpu, DType>(param);
  })
  return op;
}

// DO_BIND_DISPATCH comes from operator_common.h
Operator *OhemSoftmaxOutputProp::CreateOperatorEx(Context ctx, std::vector<TShape> *in_shape,
                                     std::vector<int> *in_type) const {
  DO_BIND_DISPATCH(CreateOp, param_, (*in_type)[0]);
}

DMLC_REGISTER_PARAMETER(OhemSoftmaxOutputParam);

MXNET_REGISTER_OP_PROPERTY(OhemSoftmaxOutput, OhemSoftmaxOutputProp)
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
.add_argument("data", "NDArray-or-Symbol", "Input array.")
.add_argument("label", "NDArray-or-Symbol", "Ground truth label.")
.add_arguments(OhemSoftmaxOutputParam::__FIELDS__());


}  // namespace op
}  // namespace mxnet
