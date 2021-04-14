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

#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_string_ref.hh"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "BKE_screen.h"

#include "RNA_access.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "BLT_translation.h"

#include "WM_api.h"
#include "WM_types.h"

#include "spreadsheet_column.hh"
#include "spreadsheet_intern.hh"
#include "spreadsheet_row_filter.hh"
#include "spreadsheet_row_filter_ui.hh"

using namespace blender;
using namespace blender::ed::spreadsheet;

static void filter_panel_id_fn(void *UNUSED(row_filter_v), char *r_name)
{
  /* All row filters use the same panel ID. */
  BLI_snprintf(r_name, BKE_ST_MAXNAME, "SPREADSHEET_PT_filter");
}

static std::string operation_string(const eSpreadsheetFilterOperation operation)
{
  switch (operation) {
    case SPREADSHEET_ROW_FILTER_EQUAL:
      return "==";
    case SPREADSHEET_ROW_FILTER_GREATER:
      return ">";
    case SPREADSHEET_ROW_FILTER_LESS:
      return "<";
  }
  BLI_assert_unreachable();
  return "";
}

static std::string value_string(const SpreadsheetRowFilter &row_filter,
                                const eSpreadsheetColumnValueType data_type)
{
  switch (data_type) {
    case SPREADSHEET_VALUE_TYPE_INT32:
      return std::to_string(row_filter.value_int);
    case SPREADSHEET_VALUE_TYPE_FLOAT: {
      std::ostringstream result;
      result.precision(3);
      result << std::fixed << row_filter.value_float;
      return result.str();
    }
    case SPREADSHEET_VALUE_TYPE_BOOL:
      return (row_filter.flag & SPREADSHEET_ROW_FILTER_BOOL_VALUE) ? IFACE_("True") :
                                                                     IFACE_("False");
    case SPREADSHEET_VALUE_TYPE_INSTANCES:
      BLI_assert_unreachable();
      return "";
  }
  BLI_assert_unreachable();
  return "";
}

static eSpreadsheetColumnValueType column_data_type_from_id(const SpaceSpreadsheet &sspreadsheet,
                                                            const SpreadsheetColumnID &column_id)
{
  LISTBASE_FOREACH (SpreadsheetColumn *, column, &sspreadsheet.columns) {
    if (*column->id == column_id) {
      return (eSpreadsheetColumnValueType)column->data_type;
    }
  }
  return SPREADSHEET_VALUE_TYPE_FLOAT;
}

static void spreadsheet_filter_panel_draw_header(const bContext *C, Panel *panel)
{
  uiLayout *layout = panel->layout;
  SpaceSpreadsheet *sspreadsheet = CTX_wm_space_spreadsheet(C);
  PointerRNA *filter_ptr = UI_panel_custom_data_get(panel);
  SpreadsheetRowFilter *filter = (SpreadsheetRowFilter *)filter_ptr->data;
  const StringRef column_name = filter->column_id->name;
  const eSpreadsheetFilterOperation operation = (const eSpreadsheetFilterOperation)
                                                    filter->operation;

  const eSpreadsheetColumnValueType data_type = column_data_type_from_id(*sspreadsheet,
                                                                         *filter->column_id);

  uiLayout *row = uiLayoutRow(layout, true);
  uiLayoutSetEmboss(row, UI_EMBOSS_NONE);
  uiItemR(row, filter_ptr, "enabled", UI_ITEM_R_ICON_ONLY, "", ICON_NONE);

  if (column_name.is_empty()) {
    uiItemL(row, IFACE_("Filter"), ICON_NONE);
  }
  else {
    std::stringstream ss;
    ss << column_name;
    ss << " ";
    ss << operation_string(operation);
    ss << " ";
    ss << value_string(*filter, data_type);
    uiItemL(row, ss.str().c_str(), ICON_NONE);
  }

  row = uiLayoutRow(layout, true);
  uiLayoutSetEmboss(row, UI_EMBOSS_NONE);
  const int current_index = BLI_findindex(&sspreadsheet->row_filters, filter);
  uiItemIntO(row, "", ICON_X, "SPREADSHEET_OT_remove_rule", "index", current_index);

  /* Some padding so the X isn't too close to the drag icon. */
  uiItemS_ex(layout, 0.25f);
}

static void spreadsheet_filter_panel_draw(const bContext *C, Panel *panel)
{
  uiLayout *layout = panel->layout;
  bScreen *screen = CTX_wm_screen(C);
  SpaceSpreadsheet *sspreadsheet = CTX_wm_space_spreadsheet(C);
  PointerRNA *filter_ptr = UI_panel_custom_data_get(panel);
  SpreadsheetRowFilter *filter = (SpreadsheetRowFilter *)filter_ptr->data;
  const eSpreadsheetFilterOperation operation = (const eSpreadsheetFilterOperation)
                                                    filter->operation;

  const eSpreadsheetColumnValueType data_type = column_data_type_from_id(*sspreadsheet,
                                                                         *filter->column_id);

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);
  uiLayoutSetActive(layout, filter->flag & SPREADSHEET_ROW_FILTER_ENABLED);

  PointerRNA column_id_ptr;
  RNA_pointer_create(&screen->id, &RNA_SpreadsheetColumnID, filter->column_id, &column_id_ptr);
  uiItemR(layout, &column_id_ptr, "name", 0, IFACE_("Column"), ICON_NONE);

  //   PointerRNA sspreadsheet_ptr;
  //   RNA_pointer_create(&screen->id, &RNA_SpreadsheetRowFilter, sspreadsheet, &sspreadsheet_ptr);
  //   uiItemPointerR(
  //       layout, filter_ptr, "column_id", &sspreadsheet_ptr, "available_columns", "", ICON_NONE);

  if (data_type != SPREADSHEET_VALUE_TYPE_BOOL) {
    uiItemR(layout, filter_ptr, "operation", 0, nullptr, ICON_NONE);
  }

  switch (data_type) {
    case SPREADSHEET_VALUE_TYPE_INT32:
      uiItemR(layout, filter_ptr, "value_int", 0, IFACE_("Value"), ICON_NONE);
      break;
    case SPREADSHEET_VALUE_TYPE_FLOAT:
      uiItemR(layout, filter_ptr, "value_float", 0, IFACE_("Value"), ICON_NONE);
      if (operation == SPREADSHEET_ROW_FILTER_EQUAL) {
        uiItemR(layout, filter_ptr, "threshold", 0, nullptr, ICON_NONE);
      }
      break;
    case SPREADSHEET_VALUE_TYPE_BOOL:
      uiItemR(layout, filter_ptr, "value_boolean", 0, IFACE_("Value"), ICON_NONE);
      break;
    case SPREADSHEET_VALUE_TYPE_INSTANCES:
      BLI_assert_unreachable();
      break;
  }
}

static void spreadsheet_row_filters_layout(const bContext *C, Panel *panel)
{
  uiLayout *layout = panel->layout;
  ARegion *region = CTX_wm_region(C);
  bScreen *screen = CTX_wm_screen(C);
  SpaceSpreadsheet *sspreadsheet = CTX_wm_space_spreadsheet(C);
  ListBase *row_filters = &sspreadsheet->row_filters;
  //   const DataSource *data_source = sspreadsheet->runtime->data_source;

  //   if (data_source != nullptr &&
  //       spreadsheet_data_source_has_selection_filter(*sspreadsheet, *data_source)) {
  PointerRNA sspreadsheet_ptr;
  RNA_pointer_create(&screen->id, &RNA_SpaceSpreadsheet, sspreadsheet, &sspreadsheet_ptr);
  uiItemR(layout, &sspreadsheet_ptr, "show_only_selected", 0, IFACE_("Selected Only"), ICON_NONE);
  //   }

  uiItemO(layout, nullptr, ICON_ADD, "SPREADSHEET_OT_add_rule");

  const bool panels_match = UI_panel_list_matches_data(region, row_filters, filter_panel_id_fn);

  if (!panels_match) {
    UI_panels_free_instanced(C, region);
    LISTBASE_FOREACH (SpreadsheetRowFilter *, row_filter, row_filters) {
      char panel_idname[MAX_NAME];
      filter_panel_id_fn(row_filter, panel_idname);

      PointerRNA *filter_ptr = (PointerRNA *)MEM_mallocN(sizeof(PointerRNA), "panel customdata");
      RNA_pointer_create(&screen->id, &RNA_SpreadsheetRowFilter, row_filter, filter_ptr);

      UI_panel_add_instanced(C, region, &region->panels, panel_idname, filter_ptr);
    }
  }
  else {
    /* Assuming there's only one group of instanced panels, update the custom data pointers. */
    Panel *panel = (Panel *)region->panels.first;
    LISTBASE_FOREACH (SpreadsheetRowFilter *, row_filter, row_filters) {

      /* Move to the next instanced panel corresponding to the next filter. */
      while ((panel->type == NULL) || !(panel->type->flag & PANEL_TYPE_INSTANCED)) {
        panel = panel->next;
        BLI_assert(panel != NULL); /* There shouldn't be fewer panels than filters. */
      }

      PointerRNA *filter_ptr = (PointerRNA *)MEM_mallocN(sizeof(PointerRNA), "panel customdata");
      RNA_pointer_create(&screen->id, &RNA_SpreadsheetRowFilter, row_filter, filter_ptr);
      UI_panel_custom_data_set(panel, filter_ptr);

      panel = panel->next;
    }
  }
}

static void filter_reorder(bContext *C, Panel *panel, int new_index)
{
  SpaceSpreadsheet *sspreadsheet = CTX_wm_space_spreadsheet(C);
  ListBase *row_filters = &sspreadsheet->row_filters;
  PointerRNA *filter_ptr = UI_panel_custom_data_get(panel);
  SpreadsheetRowFilter *filter = (SpreadsheetRowFilter *)filter_ptr->data;

  int current_index = BLI_findindex(row_filters, filter);
  BLI_assert(current_index >= 0);
  BLI_assert(new_index >= 0);

  BLI_listbase_link_move(row_filters, filter, new_index - current_index);
}

static short get_filter_expand_flag(const bContext *UNUSED(C), Panel *panel)
{
  PointerRNA *filter_ptr = UI_panel_custom_data_get(panel);
  SpreadsheetRowFilter *filter = (SpreadsheetRowFilter *)filter_ptr->data;

  return (short)filter->flag & SPREADSHEET_ROW_FILTER_UI_EXPAND;
}

static void set_filter_expand_flag(const bContext *UNUSED(C), Panel *panel, short expand_flag)
{
  PointerRNA *filter_ptr = UI_panel_custom_data_get(panel);
  SpreadsheetRowFilter *filter = (SpreadsheetRowFilter *)filter_ptr->data;

  SET_FLAG_FROM_TEST(filter->flag,
                     expand_flag & SPREADSHEET_ROW_FILTER_UI_EXPAND,
                     SPREADSHEET_ROW_FILTER_UI_EXPAND);
}

void register_row_filter_panels(ARegionType &region_type)
{
  {
    PanelType *panel_type = (PanelType *)MEM_callocN(sizeof(PanelType), __func__);
    strcpy(panel_type->idname, "SPREADSHEET_PT_row_filters");
    strcpy(panel_type->label, N_("Filters"));
    strcpy(panel_type->category, "Filters");
    strcpy(panel_type->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
    panel_type->flag = PANEL_TYPE_NO_HEADER;
    panel_type->draw = spreadsheet_row_filters_layout;
    BLI_addtail(&region_type.paneltypes, panel_type);
  }

  {
    PanelType *panel_type = (PanelType *)MEM_callocN(sizeof(PanelType), __func__);
    strcpy(panel_type->idname, "SPREADSHEET_PT_filter");
    strcpy(panel_type->label, "");
    strcpy(panel_type->category, "Filters");
    strcpy(panel_type->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
    panel_type->flag = PANEL_TYPE_INSTANCED | PANEL_TYPE_DRAW_BOX | PANEL_TYPE_HEADER_EXPAND;
    panel_type->draw_header = spreadsheet_filter_panel_draw_header;
    panel_type->draw = spreadsheet_filter_panel_draw;
    panel_type->get_list_data_expand_flag = get_filter_expand_flag;
    panel_type->set_list_data_expand_flag = set_filter_expand_flag;
    panel_type->reorder = filter_reorder;
    BLI_addtail(&region_type.paneltypes, panel_type);
  }
}
