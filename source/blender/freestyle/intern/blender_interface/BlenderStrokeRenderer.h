/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
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
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __BLENDER_STROKE_RENDERER_H__
#define __BLENDER_STROKE_RENDERER_H__

/** \file blender/freestyle/intern/blender_interface/BlenderStrokeRenderer.h
 *  \ingroup freestyle
 */

#include "../stroke/StrokeRenderer.h"
#include "../system/FreestyleConfig.h"

extern "C" {
#include "DNA_material_types.h"
#include "DNA_scene_types.h"

#include "BKE_main.h"

#include "render_types.h"
}

namespace Freestyle {

class BlenderStrokeRenderer : public StrokeRenderer
{
public:
	BlenderStrokeRenderer(Render *re, int render_count);
	virtual ~BlenderStrokeRenderer();

	/*! Renders a stroke rep */
	virtual void RenderStrokeRep(StrokeRep *iStrokeRep) const;
	virtual void RenderStrokeRepBasic(StrokeRep *iStrokeRep) const;

	Object *NewMesh() const;

	Render *RenderScene(Render *re, bool render);

protected:
	Main *freestyle_bmain;
	Scene *old_scene;
	Scene *freestyle_scene;
	float _width, _height;
	float _z, _z_delta;
	unsigned int _mesh_id;

	float get_stroke_vertex_z(void) const;
	unsigned int get_stroke_mesh_id(void) const;

#ifdef WITH_CXX_GUARDEDALLOC
	MEM_CXX_CLASS_ALLOC_FUNCS("Freestyle:BlenderStrokeRenderer")
#endif

};

} /* namespace Freestyle */

#endif // __BLENDER_STROKE_RENDERER_H__
