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

/** \file
 * \ingroup freestyle
 */

#pragma once

#include "../BPy_UnaryFunction0D.h"

#include "../../scene_graph/FrsMaterial.h"

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////

extern PyTypeObject UnaryFunction0DMaterial_Type;

#define BPy_UnaryFunction0DMaterial_Check(v) \
  (PyObject_IsInstance((PyObject *)v, (PyObject *)&UnaryFunction0DMaterial_Type))

/*---------------------------Python BPy_UnaryFunction0DMaterial structure definition----------*/
typedef struct {
  BPy_UnaryFunction0D py_uf0D;
  Freestyle::UnaryFunction0D<Freestyle::FrsMaterial> *uf0D_material;
} BPy_UnaryFunction0DMaterial;

/*---------------------------Python BPy_UnaryFunction0DMaterial visible prototypes-----------*/
int UnaryFunction0DMaterial_Init(PyObject *module);

///////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
