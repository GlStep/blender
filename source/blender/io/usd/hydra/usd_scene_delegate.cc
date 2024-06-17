/* SPDX-FileCopyrightText: 2011-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "usd_scene_delegate.hh"

#include "BLI_fileops.h"
#include "BLI_path_util.h"
#include "BLI_string.h"

#include "BKE_appdir.hh"
#include "BKE_report.hh"

#include "DEG_depsgraph_query.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "usd.hh"
#include "usd_private.hh"

using namespace blender::io::usd;

namespace blender::io::hydra {

USDSceneDelegate::USDSceneDelegate(pxr::HdRenderIndex *render_index,
                                   pxr::SdfPath const &delegate_id,
                                   const bool use_materialx)
    : render_index_(render_index), delegate_id_(delegate_id), use_materialx(use_materialx)
{
  /* Temporary directory to write any additional files to, like image or VDB files. */
  char unique_name[FILE_MAXFILE];
  SNPRINTF(unique_name, "%p", this);

  char dir_path[FILE_MAX];
  BLI_path_join(
      dir_path, sizeof(dir_path), BKE_tempdir_session(), "usd_scene_delegate", unique_name);
  BLI_dir_create_recursive(dir_path);

  char file_path[FILE_MAX];
  BLI_path_join(file_path, sizeof(file_path), dir_path, "scene.usdc");

  temp_dir_ = dir_path;
  temp_file_ = file_path;
}

USDSceneDelegate::~USDSceneDelegate()
{
  BLI_delete(temp_dir_.c_str(), true, true);
}

void USDSceneDelegate::populate(Depsgraph *depsgraph)
{
  USDExportParams params;
  params.use_instancing = true;
  params.relative_paths = false;  /* Unnecessary. */
  params.export_textures = false; /* Don't copy all textures, is slow. */
  params.evaluation_mode = DEG_get_mode(depsgraph);
  params.generate_preview_surface = !use_materialx;
  params.generate_materialx_network = use_materialx;

  /* NOTE: Since the reports list will be `nullptr` here, reports generated by export code from
   * this call will only be printed to console. */
  wmJobWorkerStatus worker_status = {};
  ReportList worker_reports = {};
  BKE_reports_init(&worker_reports, RPT_PRINT | RPT_STORE);
  worker_status.reports = &worker_reports;
  params.worker_status = &worker_status;

  /* Create clean directory for export. */
  BLI_delete(temp_dir_.c_str(), true, true);
  BLI_dir_create_recursive(temp_dir_.c_str());

  /* Free previous delegate and stage first to save memory. */
  delegate_.reset();
  stage_.Reset();

  /* Convert depsgraph to stage + additional file in temp directory. */
  stage_ = io::usd::export_to_stage(params, depsgraph, temp_file_.c_str());
  delegate_ = std::make_unique<pxr::UsdImagingDelegate>(render_index_, delegate_id_);
  delegate_->Populate(stage_->GetPseudoRoot());

  WM_reports_from_reports_move(nullptr, &worker_reports);

  BKE_reports_free(&worker_reports);
}

}  // namespace blender::io::hydra
