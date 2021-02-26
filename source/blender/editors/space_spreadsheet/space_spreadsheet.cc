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

#include <cstring>

#include "BLI_array.hh"
#include "BLI_index_range.hh"
#include "BLI_listbase.h"
#include "BLI_resource_collector.hh"

#include "BKE_editmesh.h"
#include "BKE_mesh_wrapper.h"
#include "BKE_modifier.h"
#include "BKE_screen.h"

#include "ED_screen.h"
#include "ED_space_api.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "MEM_guardedalloc.h"

#include "UI_interface.h"
#include "UI_resources.h"
#include "UI_view2d.h"

#include "DEG_depsgraph_query.h"

#include "RNA_access.h"

#include "WM_types.h"

#include "BLF_api.h"

#include "bmesh.h"

#include "spreadsheet_from_geometry.hh"
#include "spreadsheet_intern.hh"

using blender::Array;
using blender::IndexRange;
using blender::ResourceCollector;
using blender::Vector;

using namespace blender::ed::spreadsheet;

static SpaceLink *spreadsheet_create(const ScrArea *UNUSED(area), const Scene *UNUSED(scene))
{
  SpaceSpreadsheet *spreadsheet_space = (SpaceSpreadsheet *)MEM_callocN(sizeof(SpaceSpreadsheet),
                                                                        "spreadsheet space");
  spreadsheet_space->spacetype = SPACE_SPREADSHEET;

  {
    /* header */
    ARegion *region = (ARegion *)MEM_callocN(sizeof(ARegion), "spreadsheet header");
    BLI_addtail(&spreadsheet_space->regionbase, region);
    region->regiontype = RGN_TYPE_HEADER;
    region->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;
  }

  {
    /* main window */
    ARegion *region = (ARegion *)MEM_callocN(sizeof(ARegion), "spreadsheet main region");
    BLI_addtail(&spreadsheet_space->regionbase, region);
    region->regiontype = RGN_TYPE_WINDOW;
  }

  return (SpaceLink *)spreadsheet_space;
}

static void spreadsheet_free(SpaceLink *UNUSED(sl))
{
}

static void spreadsheet_init(wmWindowManager *UNUSED(wm), ScrArea *UNUSED(area))
{
}

static SpaceLink *spreadsheet_duplicate(SpaceLink *sl)
{
  return (SpaceLink *)MEM_dupallocN(sl);
}

static void spreadsheet_keymap(wmKeyConfig *UNUSED(keyconf))
{
}

static void spreadsheet_main_region_init(wmWindowManager *UNUSED(wm), ARegion *region)
{
  region->v2d.scroll = V2D_SCROLL_RIGHT | V2D_SCROLL_BOTTOM;
  region->v2d.align = V2D_ALIGN_NO_NEG_X | V2D_ALIGN_NO_POS_Y;
  region->v2d.keepzoom = V2D_LOCKZOOM_X | V2D_LOCKZOOM_Y | V2D_LIMITZOOM | V2D_KEEPASPECT;
  region->v2d.keeptot = V2D_KEEPTOT_STRICT;
  region->v2d.minzoom = region->v2d.maxzoom = 1.0f;

  UI_view2d_region_reinit(&region->v2d, V2D_COMMONVIEW_LIST, region->winx, region->winy);
}

static ID *get_used_id(const bContext *C)
{
  SpaceSpreadsheet *sspreadsheet = CTX_wm_space_spreadsheet(C);
  if (sspreadsheet->pinned_id != nullptr) {
    return sspreadsheet->pinned_id;
  }
  Object *active_object = CTX_data_active_object(C);
  return (ID *)active_object;
}

static void gather_spreadsheet_data(const bContext *C,
                                    SpreadsheetLayout &spreadsheet_layout,
                                    ResourceCollector &resources)
{
  SpaceSpreadsheet *sspreadsheet = CTX_wm_space_spreadsheet(C);

  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  ID *used_id = get_used_id(C);
  if (used_id == nullptr) {
    return;
  }
  const ID_Type id_type = GS(used_id->name);
  if (id_type != ID_OB) {
    return;
  }
  Object *object_orig = (Object *)used_id;
  if (object_orig->type != OB_MESH) {
    return;
  }
  Object *object_eval = DEG_get_evaluated_object(depsgraph, object_orig);
  if (object_eval == nullptr) {
    return;
  }

  const GeometryComponent *component = nullptr;
  GeometrySet temporary_geometry_set;

  if (object_eval->mode == OB_MODE_EDIT) {
    Mesh *mesh = BKE_modifier_get_evaluated_mesh_from_evaluated_object(object_eval, false);
    if (mesh == nullptr) {
      return;
    }
    BKE_mesh_wrapper_ensure_mdata(mesh);
    MeshComponent &mesh_component =
        temporary_geometry_set.get_component_for_write<MeshComponent>();
    mesh_component.replace(mesh, GeometryOwnershipType::ReadOnly);
    component = &mesh_component;
  }
  else {
    const GeometrySet *geometry_set = object_eval->runtime.geometry_set_eval;
    if (geometry_set != nullptr) {
      component = geometry_set->get_component_for_read<MeshComponent>();
    }
  }
  if (component == nullptr) {
    return;
  }

  const AttributeDomain domain = ATTR_DOMAIN_POINT;
  columns_from_geometry_attributes(*component, domain, resources, spreadsheet_layout);
  const int row_amount = component->attribute_domain_size(domain);
  spreadsheet_layout.row_index_digits = std::to_string(std::max(0, row_amount - 1)).size();

  const bool show_only_selected = sspreadsheet->filter_flag & SPREADSHEET_FILTER_SELECTED_ONLY;
  if (object_orig->mode == OB_MODE_EDIT && show_only_selected) {
    Vector<int64_t> &visible_rows = resources.construct<Vector<int64_t>>("visible rows");
    const MeshComponent *mesh_component = (const MeshComponent *)component;
    const Mesh *mesh_eval = mesh_component->get_for_read();
    Mesh *mesh_orig = (Mesh *)object_orig->data;
    BMesh *bm = mesh_orig->edit_mesh->bm;
    BM_mesh_elem_index_ensure(bm, BM_VERT);

    int *orig_indices = (int *)CustomData_get_layer(&mesh_eval->vdata, CD_ORIGINDEX);
    if (orig_indices != nullptr) {
      for (const int i_eval : IndexRange(mesh_eval->totvert)) {
        const int i_orig = orig_indices[i_eval];
        if (i_orig >= 0 && i_orig < bm->totvert) {
          BMVert *vert = bm->vtable[i_orig];
          if (BM_elem_flag_test(vert, BM_ELEM_SELECT)) {
            visible_rows.append(i_eval);
          }
        }
      }
    }
    else if (mesh_eval->totvert == bm->totvert) {
      for (const int i : IndexRange(mesh_eval->totvert)) {
        BMVert *vert = bm->vtable[i];
        if (BM_elem_flag_test(vert, BM_ELEM_SELECT)) {
          visible_rows.append(i);
        }
      }
    }
    spreadsheet_layout.visible_rows = visible_rows.as_span();
  }
  else {
    spreadsheet_layout.visible_rows = IndexRange(row_amount).as_span();
  }
}

static void spreadsheet_main_region_draw(const bContext *C, ARegion *region)
{
  ResourceCollector resources;

  SpreadsheetLayout spreadsheet_layout;
  gather_spreadsheet_data(C, spreadsheet_layout, resources);
  const int fontid = UI_style_get()->widget.uifont_id;
  spreadsheet_layout.index_column_width = spreadsheet_layout.row_index_digits *
                                              BLF_width(fontid, "0", 1) +
                                          UI_UNIT_X * 0.75;
  spreadsheet_layout.row_height = UI_UNIT_Y;
  spreadsheet_layout.header_row_height = 1.25 * UI_UNIT_Y;

  draw_spreadsheet_in_region(C, region, spreadsheet_layout);
}

static void spreadsheet_main_region_listener(const wmRegionListenerParams *params)
{
  /* TODO: Do more precise check. */
  ED_region_tag_redraw(params->region);
}

static void spreadsheet_header_region_init(wmWindowManager *UNUSED(wm), ARegion *region)
{
  ED_region_header_init(region);
}

static void spreadsheet_header_region_draw(const bContext *C, ARegion *region)
{
  ED_region_header(C, region);
}

static void spreadsheet_header_region_free(ARegion *UNUSED(region))
{
}

static void spreadsheet_header_region_listener(const wmRegionListenerParams *params)
{
  /* TODO: Do more precise check. */
  ED_region_tag_redraw(params->region);
}

void ED_spacetype_spreadsheet(void)
{
  SpaceType *st = (SpaceType *)MEM_callocN(sizeof(SpaceType), "spacetype spreadsheet");
  ARegionType *art;

  st->spaceid = SPACE_SPREADSHEET;
  strncpy(st->name, "Spreadsheet", BKE_ST_MAXNAME);

  st->create = spreadsheet_create;
  st->free = spreadsheet_free;
  st->init = spreadsheet_init;
  st->duplicate = spreadsheet_duplicate;
  st->operatortypes = spreadsheet_operatortypes;
  st->keymap = spreadsheet_keymap;

  /* regions: main window */
  art = (ARegionType *)MEM_callocN(sizeof(ARegionType), "spacetype spreadsheet region");
  art->regionid = RGN_TYPE_WINDOW;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D;

  art->init = spreadsheet_main_region_init;
  art->draw = spreadsheet_main_region_draw;
  art->listener = spreadsheet_main_region_listener;
  BLI_addhead(&st->regiontypes, art);

  /* regions: header */
  art = (ARegionType *)MEM_callocN(sizeof(ARegionType), "spacetype spreadsheet header region");
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = 0;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;

  art->init = spreadsheet_header_region_init;
  art->draw = spreadsheet_header_region_draw;
  art->free = spreadsheet_header_region_free;
  art->listener = spreadsheet_header_region_listener;
  BLI_addhead(&st->regiontypes, art);

  BKE_spacetype_register(st);
}
