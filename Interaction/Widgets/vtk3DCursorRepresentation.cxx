/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtk3DCursorRepresentation.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtk3DCursorRepresentation.h"
#include "vtkCollection.h"
#include "vtkCursor3D.h"
#include "vtkHardwarePicker.h"
#include "vtkMath.h"
#include "vtkObjectFactory.h"
#include "vtkOpenGLPolyDataMapper.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkRenderer.h"
#include "vtkSphereSource.h"
#include "vtkViewport.h"

#include <map>
#include <string>

VTK_ABI_NAMESPACE_BEGIN

namespace
{
// Parameters used when creating cursor shapes
double CURSOR_COLOR[3] = { 1.0, 0.0, 0.0 };
constexpr double CROSS_LINE_WIDTH = 2.0;
constexpr double SPHERE_RES = 16;
}

struct vtk3DCursorRepresentation::vtkInternals
{
  vtkInternals(vtk3DCursorRepresentation* parent)
    : Parent(parent)
  {
  }
  ~vtkInternals() = default;

  // Update the cursor actor
  void UpdateCursor();

  // Helper methods to create cursor actors
  vtkSmartPointer<vtkActor> CreateCrossCursor();
  vtkSmartPointer<vtkActor> CreateSphereCursor();

  vtk3DCursorRepresentation* Parent = nullptr;
  vtkSmartPointer<vtkActor> Cursor;
  vtkNew<vtkHardwarePicker> Picker;
  bool NeedUpdate = true;
};

//------------------------------------------------------------------------------
void vtk3DCursorRepresentation::vtkInternals::UpdateCursor()
{
  if (!this->NeedUpdate)
  {
    return;
  }

  this->NeedUpdate = false;

  switch (this->Parent->GetShape())
  {
    case CUSTOM_SHAPE:
    {
      if (this->Parent->CustomCursor)
      {
        this->Cursor = this->Parent->GetCustomCursor();
      }
      break;
    }
    case SPHERE_SHAPE:
    {
      this->Cursor = this->CreateSphereCursor();
      break;
    }
    case CROSS_SHAPE:
    default:
    {
      this->Cursor = this->CreateCrossCursor();
      break;
    }
  }
}

//------------------------------------------------------------------------------
vtkSmartPointer<vtkActor> vtk3DCursorRepresentation::vtkInternals::CreateCrossCursor()
{
  vtkNew<vtkCursor3D> cross;
  cross->AllOff();
  cross->AxesOn();

  vtkNew<vtkPolyDataMapper> mapper;
  mapper->SetInputConnection(cross->GetOutputPort());
  // Disabling it gives better results when zooming close
  // to the picked actor in the scene
  mapper->SetResolveCoincidentTopologyToOff();
  mapper->Update();

  vtkNew<vtkActor> cursor;
  cursor->SetMapper(mapper);
  cursor->GetProperty()->SetColor(CURSOR_COLOR);
  cursor->GetProperty()->SetLineWidth(CROSS_LINE_WIDTH);

  return cursor;
}

//------------------------------------------------------------------------------
vtkSmartPointer<vtkActor> vtk3DCursorRepresentation::vtkInternals::CreateSphereCursor()
{
  vtkNew<vtkSphereSource> sphere;
  sphere->SetThetaResolution(SPHERE_RES);
  sphere->SetPhiResolution(SPHERE_RES);

  vtkNew<vtkPolyDataMapper> mapper;
  mapper->SetInputConnection(sphere->GetOutputPort());
  // Disabling it gives better results when zooming close
  // to the picked actor in the scene
  mapper->SetResolveCoincidentTopologyToOff();
  mapper->Update();

  vtkNew<vtkActor> cursor;
  cursor->SetMapper(mapper);
  cursor->GetProperty()->SetColor(CURSOR_COLOR);

  return cursor;
}

//------------------------------------------------------------------------------
vtkStandardNewMacro(vtk3DCursorRepresentation);

//------------------------------------------------------------------------------
vtk3DCursorRepresentation::vtk3DCursorRepresentation()
  : Internals(new vtk3DCursorRepresentation::vtkInternals(this))
{
  this->Internals->UpdateCursor();
  this->HandleSize = 15;
  this->ValidPick = true;
}

//------------------------------------------------------------------------------
vtk3DCursorRepresentation::~vtk3DCursorRepresentation() = default;

//------------------------------------------------------------------------------
void vtk3DCursorRepresentation::WidgetInteraction(double newEventPos[2])
{
  if (!this->Renderer)
  {
    return;
  }

  vtkCollectionSimpleIterator cookie;
  vtkActorCollection* actorcol = this->Renderer->GetActors();
  vtkActor* actor;
  std::map<vtkOpenGLPolyDataMapper*, std::string> pointArrayNames;
  std::map<vtkOpenGLPolyDataMapper*, std::string> cellArrayNames;

  // Iterate through mappers to disable the potential use of point/cell data
  // arrays for selection and enforce the use of cell IDs.
  // This is needed in order to prevent a mismatch between the value retrieved
  // with hardware picking and the ID of the point/cell we want to extract before
  // computing the ray intersection (see vtkHardwarePicker).
  // This typically happen in a ParaView context, where vtkOriginalPoint/CellIds
  // is generated by the vtkPVGeometryFilter of the representation and used for
  // the hardware selection by vtkOpenGLPolyDataMapper.
  for (actorcol->InitTraversal(cookie); (actor = actorcol->GetNextActor(cookie));)
  {
    if (auto* mapper = vtkOpenGLPolyDataMapper::SafeDownCast(actor->GetMapper()))
    {
      if (mapper->GetPointIdArrayName())
      {
        pointArrayNames.emplace(mapper, mapper->GetPointIdArrayName());
        mapper->SetPointIdArrayName(nullptr);
      }
      if (mapper->GetCellIdArrayName())
      {
        cellArrayNames.emplace(mapper, mapper->GetCellIdArrayName());
        mapper->SetCellIdArrayName(nullptr);
      }
    }
  }

  this->Internals->Picker->Pick(newEventPos[0], newEventPos[1], 0.0, this->Renderer);

  // Restore the original point and cell data arrays after picking
  for (auto item = pointArrayNames.begin(); item != pointArrayNames.end(); item++)
  {
    item->first->SetPointIdArrayName(item->second.c_str());
  }
  for (auto item = cellArrayNames.begin(); item != cellArrayNames.end(); item++)
  {
    item->first->SetCellIdArrayName(item->second.c_str());
  }

  double pos[3] = { 0.0 };
  this->Internals->Picker->GetPickPosition(pos);
  this->Internals->Cursor->SetPosition(pos);
}

//------------------------------------------------------------------------------
void vtk3DCursorRepresentation::BuildRepresentation()
{
  this->Internals->UpdateCursor();

  // Target size = HandleSize in world coordinates
  double cursorPos[3] = { 0.0 };
  this->Internals->Cursor->GetPosition(cursorPos);
  double targetSize = this->SizeHandlesInPixels(1.0, cursorPos);

  double cursorBounds[6] = { 0.0 };
  this->Internals->Cursor->GetBounds(cursorBounds);

  // Safety check
  if (cursorBounds[1] - cursorBounds[0] == 0)
  {
    return;
  }

  const double sizeRatio = 2 * targetSize / (cursorBounds[1] - cursorBounds[0]);

  // Harware Picker can return NaN position when the ray cast picking do not find any
  // intersection due to floating-point arithmetic imprecisions (for example, when hitting
  // the border of a cell)
  if (std::isnan(sizeRatio))
  {
    return;
  }

  // Rescale the actor to fit the target size
  double scale[3] = { 0.0 };
  this->Internals->Cursor->GetScale(scale);
  vtkMath::MultiplyScalar(scale, sizeRatio);
  this->Internals->Cursor->SetScale(scale);
}

//------------------------------------------------------------------------------
void vtk3DCursorRepresentation::ReleaseGraphicsResources(vtkWindow* win)
{
  this->Internals->Cursor->ReleaseGraphicsResources(win);
}

//------------------------------------------------------------------------------
int vtk3DCursorRepresentation::RenderOpaqueGeometry(vtkViewport* viewport)
{
  this->BuildRepresentation();
  return this->Internals->Cursor->RenderOpaqueGeometry(viewport);
}

//------------------------------------------------------------------------------
void vtk3DCursorRepresentation::SetCursorShape(int shape)
{
  if (shape < CROSS_SHAPE || shape > CUSTOM_SHAPE)
  {
    vtkWarningMacro("Given shape doesn't exist. Valid values are ranging between "
      << CROSS_SHAPE << " and " << CUSTOM_SHAPE << ".\n Previous cursor shape is preserved.");
    return;
  }

  if (shape != this->Shape)
  {
    this->Shape = shape;
    this->Internals->NeedUpdate = true;
  }
}

//------------------------------------------------------------------------------
void vtk3DCursorRepresentation::SetCustomCursor(vtkActor* customCursor)
{
  if (customCursor && customCursor != this->CustomCursor)
  {
    this->CustomCursor = customCursor;
    this->Modified();

    if (this->Shape == CUSTOM_SHAPE)
    {
      this->Internals->NeedUpdate = true;
    }
  }
}

//------------------------------------------------------------------------------
// VTK_DEPRECATED_IN_9_3_0
void vtk3DCursorRepresentation::SetCursor(vtkActor* cursor)
{
  this->SetCursorShape(CUSTOM_SHAPE);
  this->SetCustomCursor(cursor);
}

//------------------------------------------------------------------------------
// VTK_DEPRECATED_IN_9_3_0
vtkActor* vtk3DCursorRepresentation::GetCursor()
{
  return this->GetCustomCursor();
}

//------------------------------------------------------------------------------
void vtk3DCursorRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}
VTK_ABI_NAMESPACE_END
