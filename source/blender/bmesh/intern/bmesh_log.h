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

#pragma once

/** \file
 * \ingroup bmesh
 */

struct BMFace;
struct BMVert;
struct BMesh;
struct RangeTreeUInt;

typedef struct BMLog BMLog;
typedef struct BMLogEntry BMLogEntry;

/* Allocate and initialize a new BMLog */
BMLog *BM_log_create(BMesh *bm, int cd_dyn_vert);
void BM_log_set_cd_offsets(BMLog *log, int cd_dyn_vert);

/* Allocate and initialize a new BMLog using existing BMLogEntries */
BMLog *BM_log_from_existing_entries_create(BMesh *bm, BMLogEntry *entry);

/* Free all the data in a BMLog including the log itself */
void BM_log_free(BMLog *log, bool safe_mode);

BMLog *BM_log_unfreeze(BMesh *bm, BMLogEntry *entry);

    /* Get the number of log entries */
int BM_log_length(const BMLog *log);

/* Apply a consistent ordering to BMesh vertices and faces */
void BM_log_mesh_elems_reorder(BMesh *bm, BMLog *log);

/* Start a new log entry and update the log entry list */
BMLogEntry *BM_log_entry_add(BMesh *bm, BMLog *log);
BMLogEntry *BM_log_entry_add_ex(BMesh *bm, BMLog *log, bool combine_with_last);

BMLogEntry *BM_log_entry_check_customdata(BMesh *bm, BMLog *log);

/* Mark all used ids as unused for this node */
void BM_log_cleanup_entry(BMLogEntry *entry);

/* Remove an entry from the log */
void BM_log_entry_drop(BMLogEntry *entry);

/* Undo one BMLogEntry */
void BM_log_undo(BMesh *bm, BMLog *log);

/* Redo one BMLogEntry */
void BM_log_redo(BMesh *bm, BMLog *log);

/* Log a vertex before it is modified */
void BM_log_vert_before_modified(BMLog *log,
                                 struct BMVert *v,
                                 const int cd_vert_mask_offset,
                                 bool log_customdata);

/* Log a new vertex as added to the BMesh */
void BM_log_vert_added(BMLog *log, struct BMVert *v, const int cd_vert_mask_offset);

/* Log a face before it is modified */
void BM_log_face_modified(BMLog *log, struct BMFace *f);

/* Log a new face as added to the BMesh */
void BM_log_face_added(BMLog *log, struct BMFace *f);

/* Log a vertex as removed from the BMesh */
void BM_log_vert_removed(BMLog *log, struct BMVert *v, const int cd_vert_mask_offset);

/* Log a face as removed from the BMesh */
void BM_log_face_removed(BMLog *log, struct BMFace *f);

/* Log all vertices/faces in the BMesh as added */
void BM_log_all_added(BMesh *bm, BMLog *log);

void BM_log_full_mesh(BMesh *bm, BMLog *log);

/* Log all vertices/faces in the BMesh as removed */
void BM_log_before_all_removed(BMesh *bm, BMLog *log);

/* Get the logged coordinates of a vertex */
const float *BM_log_original_vert_co(BMLog *log, BMVert *v);

/* Get the logged normal of a vertex */
const short *BM_log_original_vert_no(BMLog *log, BMVert *v);

/* Get the logged mask of a vertex */
float BM_log_original_mask(BMLog *log, BMVert *v);

/* Get the logged data of a vertex (avoid multiple lookups) */
void BM_log_original_vert_data(BMLog *log, BMVert *v, const float **r_co, const short **r_no);

/* For internal use only (unit testing) */
BMLogEntry *BM_log_current_entry(BMLog *log);
struct RangeTreeUInt *BM_log_unused_ids(BMLog *log);
