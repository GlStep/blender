/* SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 */

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"

#include "BLI_bitmap.h"
#include "BLI_ghash.h"
#include "BLI_index_range.hh"
#include "BLI_math.h"
#include "BLI_rand.h"
#include "BLI_task.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BKE_attribute.h"
#include "BKE_ccg.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_paint.h"
#include "BKE_pbvh.h"
#include "BKE_subdiv_ccg.h"

#include "PIL_time.h"

#include "GPU_buffers.h"

#include "bmesh.h"

#include "atomic_ops.h"

#include "pbvh_intern.h"

#include <climits>

using IndexRange = blender::IndexRange;

struct MLoopColHelper {
  using ColType = MLoopCol;

  static void to_float(MLoopCol *col, float r_color[4])
  {
    rgba_uchar_to_float(r_color, reinterpret_cast<const unsigned char *>(col));
    srgb_to_linearrgb_v3_v3(r_color, r_color);
  }

  static void from_float(MLoopCol *col, const float color[4])
  {
    float temp[4];

    linearrgb_to_srgb_v3_v3(temp, color);
    temp[3] = color[3];

    rgba_float_to_uchar(reinterpret_cast<unsigned char *>(col), temp);
  }
};

struct MPropColHelper {
  using ColType = MPropCol;

  static void to_float(MPropCol *col, float r_color[4])
  {
    copy_v4_v4(r_color, col->color);
  }

  static void from_float(MPropCol *col, const float color[4])
  {
    copy_v4_v4(col->color, color);
  }
};

template<typename Helper> struct ColorSetter {
  using ColType = typename Helper::ColType;

  static void set(ColType *dst, ColType *src)
  {
    *dst = *src;
  }

  static void set_float(ColType *dst, const float src[4])
  {
    Helper::from_float(dst, src);
  }
};

template<typename Helper> struct ColorSwapper {
  using ColType = typename Helper::ColType;

  static void set(ColType *dst, ColType *src)
  {
    ColType tmp = *dst;

    *dst = *src;
    *src = tmp;
  }

  static void set_float(ColType *dst, float src[4])
  {
    ColType temp = *dst;

    Helper::from_float(dst, src);
    Helper::to_float(&temp, src);
  }
};

/** Reversed setter, sets src to dst instead of dst to src. */
template<typename Helper> struct ColorStorer {
  using ColType = typename Helper::ColType;

  static void set(ColType *dst, ColType *src)
  {
    *src = *dst;
  }

  static void set_float(ColType *dst, float src[4])
  {
    Helper::to_float(dst, src);
  }
};

template<typename Helper>
static void pbvh_vertex_color_get(PBVH *pbvh, int vertex, float r_color[4])
{
  if (pbvh->vcol_domain == ATTR_DOMAIN_CORNER) {
    const MeshElemMap *melem = pbvh->pmap + vertex;
    int count = 0;

    zero_v4(r_color);

    for (int i : IndexRange(melem->count)) {
      const MPoly *mp = pbvh->mpoly + melem->indices[i];
      const MLoop *ml = pbvh->mloop + mp->loopstart;

      typename Helper::ColType *col = static_cast<typename Helper::ColType *>(pbvh->vcol->data) +
                                      mp->loopstart;

      for (int j = 0; j < mp->totloop; j++, col++, ml++) {
        if (ml->v == vertex) {
          float temp[4];
          Helper::to_float(col, temp);

          add_v4_v4(r_color, temp);
          count++;
        }
      }
    }

    if (count) {
      mul_v4_fl(r_color, 1.0f / (float)count);
    }
  }
  else {
    typename Helper::ColType *col = static_cast<typename Helper::ColType *>(pbvh->vcol->data) +
                                    vertex;
    Helper::to_float(col, r_color);
  }
}

void BKE_pbvh_vertex_color_get(PBVH *pbvh, int vertex, float r_color[4])
{
  if (pbvh->vcol->type == CD_PROP_COLOR) {
    pbvh_vertex_color_get<MPropColHelper>(pbvh, vertex, r_color);
  }
  else {
    pbvh_vertex_color_get<MLoopColHelper>(pbvh, vertex, r_color);
  }
}

template<typename Helper, typename Setter = ColorSetter<Helper>>
static void pbvh_vertex_color_set(PBVH *pbvh, int vertex, const float color[4])
{
  if (pbvh->vcol_domain == ATTR_DOMAIN_CORNER) {
    const MeshElemMap *melem = pbvh->pmap + vertex;

    for (int i : IndexRange(melem->count)) {
      const MPoly *mp = pbvh->mpoly + melem->indices[i];
      const MLoop *ml = pbvh->mloop + mp->loopstart;

      typename Helper::ColType *col = static_cast<typename Helper::ColType *>(pbvh->vcol->data) +
                                      mp->loopstart;

      for (int j = 0; j < mp->totloop; j++, col++, ml++) {
        if (ml->v == vertex) {
          Setter::set_float(col, color);
        }
      }
    }
  }
  else {
    typename Helper::ColType *col = static_cast<typename Helper::ColType *>(pbvh->vcol->data) +
                                    vertex;
    Setter::set_float(col, color);
  }
}

void BKE_pbvh_vertex_color_set(PBVH *pbvh, int vertex, const float color[4])
{
  if (pbvh->vcol->type == CD_PROP_COLOR) {
    pbvh_vertex_color_set<MPropColHelper>(pbvh, vertex, color);
  }
  else {
    pbvh_vertex_color_set<MLoopColHelper>(pbvh, vertex, color);
  }
}

template<typename Helper, typename Setter = ColorSetter<Helper>>
static void pbvh_set_colors(
    PBVH *UNUSED(pbvh), void *color_attr, float (*colors)[4], int *indices, int indices_num)
{
  typename Helper::ColType *col = reinterpret_cast<typename Helper::ColType *>(color_attr);

  for (int i : IndexRange(indices_num)) {
    Setter::set_float(col + indices[i], colors[i]);
  }
}

void BKE_pbvh_swap_colors(PBVH *pbvh, float (*colors)[4], int *indices, int indices_num)
{
  if (pbvh->vcol->type == CD_PROP_COLOR) {
    pbvh_set_colors<MPropColHelper, ColorSwapper<MPropColHelper>>(
        pbvh, pbvh->vcol->data, colors, indices, indices_num);
  }
  else {
    pbvh_set_colors<MLoopColHelper, ColorSwapper<MLoopColHelper>>(
        pbvh, pbvh->vcol->data, colors, indices, indices_num);
  }
}

void BKE_pbvh_store_colors(PBVH *pbvh, float (*colors)[4], int *indices, int indices_num)
{
  if (pbvh->vcol->type == CD_PROP_COLOR) {
    pbvh_set_colors<MPropColHelper, ColorStorer<MPropColHelper>>(
        pbvh, pbvh->vcol->data, colors, indices, indices_num);
  }
  else {
    pbvh_set_colors<MLoopColHelper, ColorStorer<MLoopColHelper>>(
        pbvh, pbvh->vcol->data, colors, indices, indices_num);
  }
}

void BKE_pbvh_store_colors_vertex(PBVH *pbvh, float (*colors)[4], int *indices, int indices_num)
{
  if (pbvh->vcol_domain == ATTR_DOMAIN_POINT) {
    BKE_pbvh_store_colors(pbvh, colors, indices, indices_num);
  }
  else {
    for (int i = 0; i < indices_num; i++) {
      BKE_pbvh_vertex_color_get(pbvh, indices[i], colors[i]);
    }
  }
}
