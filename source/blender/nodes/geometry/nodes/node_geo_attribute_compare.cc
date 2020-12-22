/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "node_geometry_util.hh"

#include "BKE_attribute.h"
#include "BKE_attribute_access.hh"

#include "BLI_array.hh"
#include "BLI_math_base_safe.h"
#include "BLI_rand.hh"

#include "DNA_mesh_types.h"
#include "DNA_pointcloud_types.h"

#include "NOD_math_functions.hh"

static bNodeSocketTemplate geo_node_attribute_compare_in[] = {
    {SOCK_GEOMETRY, N_("Geometry")},
    {SOCK_STRING, N_("A")},
    {SOCK_FLOAT, N_("A"), 0.0, 0.0, 0.0, 0.0, -FLT_MAX, FLT_MAX},
    {SOCK_VECTOR, N_("A"), 0.0, 0.0, 0.0, 0.0, -FLT_MAX, FLT_MAX},
    {SOCK_RGBA, N_("A"), 0.5, 0.5, 0.5, 1.0},
    {SOCK_STRING, N_("B")},
    {SOCK_FLOAT, N_("B"), 0.0, 0.0, 0.0, 0.0, -FLT_MAX, FLT_MAX},
    {SOCK_VECTOR, N_("B"), 0.0, 0.0, 0.0, 0.0, -FLT_MAX, FLT_MAX},
    {SOCK_RGBA, N_("B"), 0.5, 0.5, 0.5, 1.0},
    {SOCK_FLOAT, N_("Threshold"), 0.01f, 0.0f, 0.0f, 0.0f, 0.0f, FLT_MAX},
    {SOCK_STRING, N_("Result")},
    {-1, ""},
};

static bNodeSocketTemplate geo_node_attribute_compare_out[] = {
    {SOCK_GEOMETRY, N_("Geometry")},
    {-1, ""},
};

static void geo_node_attribute_compare_init(bNodeTree *UNUSED(tree), bNode *node)
{
  NodeAttributeCompare *data = (NodeAttributeCompare *)MEM_callocN(sizeof(NodeAttributeCompare),
                                                                   "attribute mix node");
  data->operation = NODE_FLOAT_COMPARE_GREATER_THAN;
  data->input_type_a = GEO_NODE_ATTRIBUTE_INPUT_ATTRIBUTE;
  data->input_type_b = GEO_NODE_ATTRIBUTE_INPUT_ATTRIBUTE;
  node->storage = data;
}

static bool operation_tests_equality(const NodeAttributeCompare &node_storage)
{
  return ELEM(node_storage.operation, NODE_FLOAT_COMPARE_EQUAL, NODE_FLOAT_COMPARE_NOT_EQUAL);
}

namespace blender::nodes {

static void geo_node_attribute_compare_update(bNodeTree *UNUSED(ntree), bNode *node)
{
  NodeAttributeCompare *node_storage = (NodeAttributeCompare *)node->storage;
  update_attribute_input_socket_availabilities(
      *node, "A", (GeometryNodeAttributeInputMode)node_storage->input_type_a);
  update_attribute_input_socket_availabilities(
      *node, "B", (GeometryNodeAttributeInputMode)node_storage->input_type_b);

  bNodeSocket *socket_threshold = (bNodeSocket *)BLI_findlink(&node->inputs, 9);
  nodeSetSocketAvailability(socket_threshold, operation_tests_equality(*node_storage));
}

static void do_math_operation(const FloatReadAttribute &input_a,
                              const FloatReadAttribute &input_b,
                              const FloatCompareOperation operation,
                              MutableSpan<bool> span_result)
{
  const int size = input_a.size();

  Span<float> span_a = input_a.get_span();
  Span<float> span_b = input_b.get_span();

  if (try_dispatch_float_math_fl_fl_to_bool(
          operation, [&](auto math_function, const FloatMathOperationInfo &UNUSED(info)) {
            for (const int i : IndexRange(size)) {
              const float a = span_a[i];
              const float b = span_b[i];
              const bool out = math_function(a, b);
              span_result[i] = out;
            }
          })) {
    return;
  }

  /* The operation is not supported by this node currently. */
  BLI_assert(false);
}

static void do_equal_operation(const FloatReadAttribute &input_a,
                               const FloatReadAttribute &input_b,
                               const float threshold,
                               MutableSpan<bool> span_result)
{
  const int size = input_a.size();
  for (const int i : IndexRange(size)) {
    const float a = input_a[i];
    const float b = input_b[i];
    span_result[i] = compare_ff(a, b, threshold);
  }
}

static void do_equal_operation(const Float3ReadAttribute &input_a,
                               const Float3ReadAttribute &input_b,
                               const float threshold,
                               MutableSpan<bool> span_result)
{
  const float threshold_squared = pow2f(threshold);
  const int size = input_a.size();
  for (const int i : IndexRange(size)) {
    const float3 a = input_a[i];
    const float3 b = input_b[i];
    span_result[i] = len_squared_v3v3(a, b) < threshold_squared;
  }
}

static void do_equal_operation(const Color4fReadAttribute &input_a,
                               const Color4fReadAttribute &input_b,
                               const float threshold,
                               MutableSpan<bool> span_result)
{
  const float threshold_squared = pow2f(threshold);
  const int size = input_a.size();
  for (const int i : IndexRange(size)) {
    const Color4f a = input_a[i];
    const Color4f b = input_b[i];
    span_result[i] = len_squared_v4v4(a, b) < threshold_squared;
  }
}

static void do_equal_operation(const BooleanReadAttribute &input_a,
                               const BooleanReadAttribute &input_b,
                               const float UNUSED(threshold),
                               MutableSpan<bool> span_result)
{
  const int size = input_a.size();
  for (const int i : IndexRange(size)) {
    const bool a = input_a[i];
    const bool b = input_b[i];
    span_result[i] = a == b;
  }
}

static void do_not_equal_operation(const FloatReadAttribute &input_a,
                                   const FloatReadAttribute &input_b,
                                   const float threshold,
                                   MutableSpan<bool> span_result)
{
  const int size = input_a.size();
  for (const int i : IndexRange(size)) {
    const float a = input_a[i];
    const float b = input_b[i];
    span_result[i] = !compare_ff(a, b, threshold);
  }
}

static void do_not_equal_operation(const Float3ReadAttribute &input_a,
                                   const Float3ReadAttribute &input_b,
                                   const float threshold,
                                   MutableSpan<bool> span_result)
{
  const float threshold_squared = pow2f(threshold);
  const int size = input_a.size();
  for (const int i : IndexRange(size)) {
    const float3 a = input_a[i];
    const float3 b = input_b[i];
    span_result[i] = len_squared_v3v3(a, b) >= threshold_squared;
  }
}

static void do_not_equal_operation(const Color4fReadAttribute &input_a,
                                   const Color4fReadAttribute &input_b,
                                   const float threshold,
                                   MutableSpan<bool> span_result)
{
  const float threshold_squared = pow2f(threshold);
  const int size = input_a.size();
  for (const int i : IndexRange(size)) {
    const Color4f a = input_a[i];
    const Color4f b = input_b[i];
    span_result[i] = len_squared_v4v4(a, b) >= threshold_squared;
  }
}

static void do_not_equal_operation(const BooleanReadAttribute &input_a,
                                   const BooleanReadAttribute &input_b,
                                   const float UNUSED(threshold),
                                   MutableSpan<bool> span_result)
{
  const int size = input_a.size();
  for (const int i : IndexRange(size)) {
    const bool a = input_a[i];
    const bool b = input_b[i];
    span_result[i] = a != b;
  }
}

static CustomDataType get_data_type(GeometryComponent &component,
                                    const GeoNodeExecParams &params,
                                    const NodeAttributeCompare &node_storage)
{
  if (operation_tests_equality(node_storage)) {
    CustomDataType data_type_a = params.get_input_attribute_data_type(
        "A", component, CD_PROP_FLOAT);
    CustomDataType data_type_b = params.get_input_attribute_data_type(
        "B", component, CD_PROP_FLOAT);

    /* Convert the input attributes to the same data type for the equality tests. Use the higher
     * complexity attribute type, otherwise information necessary to the comparison may be lost. */
    return attribute_domain_highest_complexity({data_type_a, data_type_b});
  }

  /* Use float compare for every operation besides equality. */
  return CD_PROP_FLOAT;
}

static void attribute_compare_calc(GeometryComponent &component, const GeoNodeExecParams &params)
{
  const bNode &node = params.node();
  NodeAttributeCompare *node_storage = (NodeAttributeCompare *)node.storage;
  const FloatCompareOperation operation = static_cast<FloatCompareOperation>(
      node_storage->operation);

  /* The result type of this node is always float. */
  const CustomDataType result_type = CD_PROP_BOOL;
  /* The result domain is always point for now. */
  const AttributeDomain result_domain = ATTR_DOMAIN_POINT;

  /* Get result attribute first, in case it has to overwrite one of the existing attributes. */
  const std::string result_name = params.get_input<std::string>("Result");
  WriteAttributePtr attribute_result = component.attribute_try_ensure_for_write(
      result_name, result_domain, result_type);
  if (!attribute_result) {
    return;
  }

  const CustomDataType input_data_type = get_data_type(component, params, *node_storage);

  ReadAttributePtr attribute_a = params.get_input_attribute(
      "A", component, result_domain, input_data_type, nullptr);
  ReadAttributePtr attribute_b = params.get_input_attribute(
      "B", component, result_domain, input_data_type, nullptr);

  if (!attribute_a || !attribute_b) {
    /* Attribute wasn't found. */
    return;
  }

  BooleanWriteAttribute attribute_result_bool = std::move(attribute_result);
  MutableSpan<bool> result_span = attribute_result_bool.get_span();

  /* Use specific types for correct equality operations, but for other operations we use implicit
   * conversions and float comparison. In other words, the comparison is not element-wise. */
  if (operation_tests_equality(*node_storage)) {
    const float threshold = params.get_input<float>("Threshold");
    if (operation == NODE_FLOAT_COMPARE_EQUAL) {
      if (input_data_type == CD_PROP_FLOAT) {
        FloatReadAttribute attribute_a_float = std::move(attribute_a);
        FloatReadAttribute attribute_b_float = std::move(attribute_b);
        do_equal_operation(
            std::move(attribute_a_float), std::move(attribute_b_float), threshold, result_span);
      }
      else if (input_data_type == CD_PROP_FLOAT3) {
        Float3ReadAttribute attribute_a_float3 = std::move(attribute_a);
        Float3ReadAttribute attribute_b_float3 = std::move(attribute_b);
        do_equal_operation(
            std::move(attribute_a_float3), std::move(attribute_b_float3), threshold, result_span);
      }
      else if (input_data_type == CD_PROP_COLOR) {
        Color4fReadAttribute attribute_a_color = std::move(attribute_a);
        Color4fReadAttribute attribute_b_color = std::move(attribute_b);
        do_equal_operation(
            std::move(attribute_a_color), std::move(attribute_b_color), threshold, result_span);
      }
      else if (input_data_type == CD_PROP_BOOL) {
        BooleanReadAttribute attribute_a_bool = std::move(attribute_a);
        BooleanReadAttribute attribute_b_bool = std::move(attribute_b);
        do_equal_operation(
            std::move(attribute_a_bool), std::move(attribute_b_bool), threshold, result_span);
      }
    }
    else if (operation == NODE_FLOAT_COMPARE_NOT_EQUAL) {
      if (input_data_type == CD_PROP_FLOAT) {
        FloatReadAttribute attribute_a_float = std::move(attribute_a);
        FloatReadAttribute attribute_b_float = std::move(attribute_b);
        do_not_equal_operation(
            std::move(attribute_a_float), std::move(attribute_b_float), threshold, result_span);
      }
      else if (input_data_type == CD_PROP_FLOAT3) {
        Float3ReadAttribute attribute_a_float3 = std::move(attribute_a);
        Float3ReadAttribute attribute_b_float3 = std::move(attribute_b);
        do_not_equal_operation(
            std::move(attribute_a_float3), std::move(attribute_b_float3), threshold, result_span);
      }
      else if (input_data_type == CD_PROP_COLOR) {
        Color4fReadAttribute attribute_a_color = std::move(attribute_a);
        Color4fReadAttribute attribute_b_color = std::move(attribute_b);
        do_not_equal_operation(
            std::move(attribute_a_color), std::move(attribute_b_color), threshold, result_span);
      }
      else if (input_data_type == CD_PROP_BOOL) {
        BooleanReadAttribute attribute_a_bool = std::move(attribute_a);
        BooleanReadAttribute attribute_b_bool = std::move(attribute_b);
        do_not_equal_operation(
            std::move(attribute_a_bool), std::move(attribute_b_bool), threshold, result_span);
      }
    }
  }
  else {
    do_math_operation(std::move(attribute_a), std::move(attribute_b), operation, result_span);
  }

  attribute_result_bool.apply_span();
}

static void geo_node_attribute_compare_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Geometry");

  if (geometry_set.has<MeshComponent>()) {
    attribute_compare_calc(geometry_set.get_component_for_write<MeshComponent>(), params);
  }
  if (geometry_set.has<PointCloudComponent>()) {
    attribute_compare_calc(geometry_set.get_component_for_write<PointCloudComponent>(), params);
  }

  params.set_output("Geometry", geometry_set);
}

}  // namespace blender::nodes

void register_node_type_geo_attribute_compare()
{
  static bNodeType ntype;

  geo_node_type_base(
      &ntype, GEO_NODE_ATTRIBUTE_COMPARE, "Attribute Compare", NODE_CLASS_ATTRIBUTE, 0);
  node_type_socket_templates(
      &ntype, geo_node_attribute_compare_in, geo_node_attribute_compare_out);
  ntype.geometry_node_execute = blender::nodes::geo_node_attribute_compare_exec;
  node_type_update(&ntype, blender::nodes::geo_node_attribute_compare_update);
  node_type_storage(
      &ntype, "NodeAttributeCompare", node_free_standard_storage, node_copy_standard_storage);
  node_type_init(&ntype, geo_node_attribute_compare_init);
  nodeRegisterType(&ntype);
}
