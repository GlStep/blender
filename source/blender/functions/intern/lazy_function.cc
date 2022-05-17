/* SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup fn
 */

#include "BLI_array.hh"

#include "FN_lazy_function.hh"

namespace blender::fn {

std::string LazyFunction::name() const
{
  return static_name_;
}

std::string LazyFunction::input_name(int index) const
{
  return inputs_[index].static_name;
}

std::string LazyFunction::output_name(int index) const
{
  return outputs_[index].static_name;
}

class EagerLazyFunctionParams : public LazyFunctionParams {
 private:
  const Span<GMutablePointer> inputs_;
  const Span<GMutablePointer> outputs_;
#ifdef DEBUG
  Array<bool> set_outputs_;
#endif

 public:
  EagerLazyFunctionParams(const LazyFunction &fn,
                          const Span<GMutablePointer> inputs,
                          const Span<GMutablePointer> outputs)
      : LazyFunctionParams(fn), inputs_(inputs), outputs_(outputs)
  {
#ifdef DEBUG
    set_outputs_.reinitialize(fn.outputs().size());
    set_outputs_.fill(false);
#endif
  }

  ~EagerLazyFunctionParams()
  {
#ifdef DEBUG
    /* Check that all outputs have been initialized. */
    for (const int i : set_outputs_.index_range()) {
      BLI_assert(set_outputs_[i]);
    }
#endif
  }

  void *try_get_input_data_ptr_impl(int index) override
  {
    return inputs_[index].get();
  }

  void *get_output_data_ptr_impl(int index) override
  {
    return outputs_[index].get();
  }

  void output_set_impl(int index) override
  {
#ifdef DEBUG
    set_outputs_[index] = true;
#endif
    UNUSED_VARS_NDEBUG(index);
  }

  ValueUsage get_output_usage_impl(int UNUSED(index)) override
  {
    return ValueUsage::Used;
  }

  void set_input_unused_impl(int UNUSED(index)) override
  {
  }
};

void LazyFunction::execute_eager(const Span<GMutablePointer> inputs,
                                 const Span<GMutablePointer> outputs) const
{
  EagerLazyFunctionParams params{*this, inputs, outputs};
  this->execute(params);
}

}  // namespace blender::fn
