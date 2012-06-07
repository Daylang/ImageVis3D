/*
   For more information, please see: http://software.sci.utah.edu

   The MIT License

   Copyright (c) 2008 Scientific Computing and Imaging Institute,
   University of Utah.


   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/


//!    File   : RenderWindow.cpp
//!    Author : Jens Krueger
//!             SCI Institute
//!             University of Utah
//!    Date   : July 2008
//
//!    Copyright (C) 2008 SCI Institute

#include "../Tuvok/StdTuvokDefines.h"
#include <cassert>
#include <sstream>
#include <stdexcept>
#include "GL/glew.h"
#if defined(__GNUC__) && defined(DETECTED_OS_LINUX)
# pragma GCC visibility push(default)
#endif
#include <QtGui/QtGui>
#include <QtGui/QMessageBox>
#if defined(__GNUC__) && defined(DETECTED_OS_LINUX)
# pragma GCC visibility pop
#endif

#include "RenderWindow.h"

#include "ImageVis3D.h"
#include "../Tuvok/Basics/MathTools.h"
#include "../Tuvok/Basics/SysTools.h"
#include "../Tuvok/Controller/Controller.h"
#include "../Tuvok/Renderer/GL/GLFrameCapture.h"
#include "../Tuvok/Renderer/GL/GLFBOTex.h"
#include "../Tuvok/Renderer/GL/GLRenderer.h"
#include "../Tuvok/Renderer/GL/GLTargetBinder.h"
#include "../Tuvok/LuaScripting/LuaScripting.h"
#include "../Tuvok/LuaScripting/TuvokSpecific/LuaTuvokTypes.h"
#include "Basics/tr1.h"

using namespace std;
using namespace tuvok;

std::string RenderWindow::ms_gpuVendorString = "";
uint32_t RenderWindow::ms_iMaxVolumeDims = 0;
bool RenderWindow::ms_b3DTexInDriver = false;
bool RenderWindow::ms_bImageLoadStoreInDriver = false;

RenderWindow::RenderWindow(MasterController& masterController,
                           MasterController::EVolumeRendererType eType,
                           const QString& dataset,
                           unsigned int iCounter,
                           QWidget* parent,
                           const UINTVECTOR2& vMinSize,
                           const UINTVECTOR2& vDefaultSize) :
  m_strDataset(dataset),
  m_strID(""),
  m_Renderer(NULL),
  m_MasterController(masterController),
  m_bRenderSubsysOK(true),   // be optimistic :-)
  selectedRegionSplitter(REGION_SPLITTER_NONE),
  m_vWinDim(0,0),
  m_vMinSize(vMinSize),
  m_vDefaultSize(vDefaultSize),

  m_eViewMode(VM_SINGLE),
  m_vWinFraction(0.5, 0.5),
  activeRegion(NULL),
  m_MainWindow((MainWindow*)parent),
  m_eRendererType(eType),
  m_iTimeSliceMSecsActive(500),
  m_iTimeSliceMSecsInActive(100),
  m_1DHistScale(0.25f),
  m_2DHistScale(0.75f),
  initialClickPos(0,0),
  m_viMousePos(0,0),
  m_bAbsoluteViewLock(true),
  m_bInvWheel(false),
  m_RTModeBeforeCapture(AbstrRenderer::RT_INVALID_MODE),
  m_SavedClipLocked(true)
{
  m_strID = "[%1] %2";
  m_strID = m_strID.arg(iCounter).arg(dataset);
}

RenderWindow::~RenderWindow()
{
  Cleanup();

  // We notify the LuaScripting system because there are instances where this
  // class is destroyed and deleteClass was not called upon it.
  m_MasterController.LuaScript()->notifyOfDeletion(m_LuaThisClass);
}


void RenderWindow::ToggleHQCaptureMode() {
  /// @todo call explicitly through the command system to enable provenance.
  ///       It is not entirely clear that this should be the only function
  ///       called however.
  if (m_Renderer->GetRendererTarget() == AbstrRenderer::RT_CAPTURE) {
    EnableHQCaptureMode(false);
  } else {
    EnableHQCaptureMode(true);
  }
}

void RenderWindow::EnableHQCaptureMode(bool enable) {
  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();
  string abstrRenName = GetLuaAbstrRenderer().fqName();

  if (m_Renderer->GetRendererTarget() == AbstrRenderer::RT_CAPTURE) {
    // restore rotation from before the capture process
    if (enable == false) {
      /// @fixme Shouldn't this be GetFirst3DRegion?
      SetRotation(GetActiveRenderRegions()[0], m_mCaptureStartRotation, false);
      ss->cexec(abstrRenName + ".setRendererTarget", m_RTModeBeforeCapture);
    }
  } else {
    // remember rotation from before the capture process
    if (enable == true) {
      m_RTModeBeforeCapture = ss->cexecRet<AbstrRenderer::ERendererTarget>(
          abstrRenName + ".getRendererTarget");
      m_mCaptureStartRotation = GetRotation(GetActiveRenderRegions()[0]);
      ss->cexec(abstrRenName + ".setRendererTarget", AbstrRenderer::RT_CAPTURE);
    }
  }
}

FLOATMATRIX4 RenderWindow::GetRotation(LuaClassInstance region)
{
  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();
  string arn = GetLuaAbstrRenderer().fqName();

  FLOATMATRIX4 regionRot =
      ss->cexecRet<FLOATMATRIX4>(arn + ".getRegionRotation4x4", region);
  return regionRot;
}

FLOATMATRIX4 RenderWindow::GetTranslation(LuaClassInstance region)
{
  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();
  string arn = GetLuaAbstrRenderer().fqName();

  FLOATMATRIX4 regionTrans =
      ss->cexecRet<FLOATMATRIX4>(arn + ".setRegionTranslation4x4", region);
  return regionTrans;
}

void RenderWindow::Translate(const FLOATMATRIX4& translation,
                             LuaClassInstance region) {
  if(region.isDefaultInstance()) {
    region = GetFirst3DRegion();
  }
  if(region.isDefaultInstance() == false) {
    /// @todo 4x4 matrix mult -> vector addition.
    FLOATMATRIX4 regionTrans = GetTranslation(region);
    SetTranslation(region, translation * regionTrans, false);
  }
}

void RenderWindow::Rotate(const FLOATMATRIX4& rotation,
                          LuaClassInstance region) {
  if(region.isDefaultInstance()) {
    region = GetFirst3DRegion();
  }
  if(region.isDefaultInstance() == false) {
    FLOATMATRIX4 regionRot = GetRotation(region);
    SetRotation(region, rotation * regionRot, false);
  }
}

void RenderWindow::SetCaptureRotationAngle(float fAngle) {
  FLOATMATRIX4 matRot;
  matRot.RotationY(3.141592653589793238462643383*double(fAngle)/180.0);
  matRot = m_mCaptureStartRotation * matRot;
  /// @fixme Is the lack of provenance on this next call correct?
  SetRotation(GetActiveRenderRegions()[0], matRot, false);
  PaintRenderer();
}

RenderWindow::RegionData*
RenderWindow::GetRegionData(LuaClassInstance renderRegion) const
{
#ifdef TR1_NOT_CONST_CORRECT
  RenderWindow *cthis = const_cast<RenderWindow*>(this);
  RegionDataMap::const_iterator iter = cthis->regionDataMap.find(
      renderRegion.getGlobalInstID());
#else
  RegionDataMap::const_iterator iter = regionDataMap.find(renderRegion);
#endif
  if (iter == regionDataMap.end()) {
    // This should never happen if the renderRegion belongs to *this.
    assert(false);
    return NULL;
  }
  return iter->second;
}

uint64_t RenderWindow::GetSliceDepth(LuaClassInstance renderRegion) const {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  string arn = GetLuaAbstrRenderer().fqName();
  return ss->cexecRet<uint64_t>(arn + ".getRegionSliceDepth", renderRegion);
}

void RenderWindow::SetSliceDepth(LuaClassInstance renderRegion,
                                 uint64_t newDepth) {
  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();
  string arn = GetLuaAbstrRenderer().fqName();
  ss->cexec(arn + ".setRegionSliceDepth", renderRegion, newDepth);
}

bool RenderWindow::IsRegion2D(LuaClassInstance region) const {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  RenderRegion* regPtr = region.getRawPointer<RenderRegion>(ss);
  return regPtr->is2D();
}

bool RenderWindow::IsRegion3D(LuaClassInstance region) const {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  RenderRegion* regPtr = region.getRawPointer<RenderRegion>(ss);
  return regPtr->is3D();
}

//ContainsPoint(UINTVECTOR2 pos
bool RenderWindow::DoesRegionContainPoint(LuaClassInstance region,
                                       UINTVECTOR2 pos) const {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
//return ss->cexecRet<bool>(GetActiveRenderRegions()[i].fqName() +
//                          ".containsPoint", UINTVECTOR2(vPos));
  RenderRegion* regPtr = region.getRawPointer<RenderRegion>(ss);
  return regPtr->ContainsPoint(pos);
}

RenderRegion::EWindowMode RenderWindow::GetRegionWindowMode(
    tuvok::LuaClassInstance region) const {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  RenderRegion* regPtr = region.getRawPointer<RenderRegion>(ss);
  return regPtr->windowMode;
}

bool RenderWindow::Get2DFlipModeX(tuvok::LuaClassInstance region) const {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  string arn = GetLuaAbstrRenderer().fqName();
  return ss->cexecRet<bool>(arn + ".getRegion2DFlipModeX", region);
}

bool RenderWindow::Get2DFlipModeY(tuvok::LuaClassInstance region) const {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  string arn = GetLuaAbstrRenderer().fqName();
  return ss->cexecRet<bool>(arn + ".getRegion2DFlipModeY", region);
}

void RenderWindow::Set2DFlipMode(tuvok::LuaClassInstance region, bool flipX,
                                 bool flipY) {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  string arn = GetLuaAbstrRenderer().fqName();
  return ss->cexec(arn + ".setRegion2DFlipMode", region, flipX, flipY);
}

bool RenderWindow::GetUseMIP(tuvok::LuaClassInstance region) {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  string arn = GetLuaAbstrRenderer().fqName();
  return ss->cexecRet<bool>(arn + ".getRegionUseMIP", region);
}

void RenderWindow::SetUseMIP(tuvok::LuaClassInstance region, bool useMip) {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  string arn = GetLuaAbstrRenderer().fqName();
  ss->cexec(arn + ".setRegionUseMIP", region, useMip);
}

UINTVECTOR2 RenderWindow::GetRegionMinCoord(tuvok::LuaClassInstance region)const
{
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  RenderRegion* regPtr = region.getRawPointer<RenderRegion>(ss);
  return regPtr->minCoord;
}

UINTVECTOR2 RenderWindow::GetRegionMaxCoord(tuvok::LuaClassInstance region)const
{
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  RenderRegion* regPtr = region.getRawPointer<RenderRegion>(ss);
  return regPtr->maxCoord;
}

void RenderWindow::SetRegionMinCoord(tuvok::LuaClassInstance region,
                                     UINTVECTOR2 minCoord) {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  RenderRegion* regPtr = region.getRawPointer<RenderRegion>(ss);
  regPtr->minCoord = minCoord;
}
void RenderWindow::SetRegionMaxCoord(tuvok::LuaClassInstance region,
                                     UINTVECTOR2 maxCoord) {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  RenderRegion* regPtr = region.getRawPointer<RenderRegion>(ss);
  regPtr->maxCoord = maxCoord;
}

RenderWindow::RegionSplitter RenderWindow::GetRegionSplitter(INTVECTOR2 pos) const
{
  switch (m_eViewMode) {
    case VM_SINGLE   : return REGION_SPLITTER_NONE;
    case VM_TWOBYTWO :
      {
        const int halfWidth = regionSplitterWidth/2;
        const INTVECTOR2 splitPoint(m_vWinFraction * FLOATVECTOR2(m_vWinDim));
        const bool isVertical   = abs(pos.x - splitPoint.x) <= halfWidth;
        const bool isHorizontal = abs(pos.y - splitPoint.y) <= halfWidth;

        if (isVertical && isHorizontal) return REGION_SPLITTER_BOTH_2x2;
        if (isVertical)                 return REGION_SPLITTER_VERTICAL_2x2;
        if (isHorizontal)               return REGION_SPLITTER_HORIZONTAL_2x2;
        return REGION_SPLITTER_NONE;
      }
      break;
    default : return REGION_SPLITTER_NONE;
  }
}

void RenderWindow::MousePressEvent(QMouseEvent *event)
{
  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();

  activeRegion = GetRegionUnderCursor(m_viMousePos);

  if (activeRegion.isDefaultInstance() == false) {
    // mouse is over the 3D window
    if (IsRegion3D(activeRegion) ) {
      SetPlaneAtClick(m_ClipPlane);

      if (event->button() == Qt::RightButton)
        initialClickPos = INTVECTOR2(event->pos().x(), event->pos().y());

      if (event->button() == Qt::LeftButton) {
        RegionData *regionData = GetRegionData(activeRegion);
        regionData->clipArcBall.Click(UINTVECTOR2(event->pos().x(), event->pos().y()));
        if ( !(event->modifiers() & Qt::ControlModifier) ) {
          regionData->arcBall.Click(UINTVECTOR2(event->pos().x(), event->pos().y()));
        }
      }
    }
  } else { // Probably clicked on a region separator.
    selectedRegionSplitter = GetRegionSplitter(m_viMousePos);
    if (selectedRegionSplitter != REGION_SPLITTER_NONE) {
      initialClickPos = INTVECTOR2(event->pos().x(), event->pos().y());
    }
  }
}

void RenderWindow::MouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    if (!activeRegion.isDefaultInstance())
      FinalizeRotation(activeRegion, true);
  }

  selectedRegionSplitter = REGION_SPLITTER_NONE;

  LuaClassInstance region = GetRegionUnderCursor(m_viMousePos);
  UpdateCursor(region, m_viMousePos, false);
}

// Qt callback; just interprets the event and passes it on to the appropriate
// ImageVis3D handler.
void RenderWindow::MouseMoveEvent(QMouseEvent *event)
{
  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();

  m_viMousePos = INTVECTOR2(event->pos().x(), event->pos().y());

  LuaClassInstance region = GetRegionUnderCursor(m_viMousePos);

  bool clip = event->modifiers() & Qt::ControlModifier;
  bool clearview = event->modifiers() & Qt::ShiftModifier;
  bool rotate = event->buttons() & Qt::LeftButton;
  bool translate = event->buttons() & Qt::RightButton;

  if (selectedRegionSplitter != REGION_SPLITTER_NONE) {
    region = LuaClassInstance();
  }

  UpdateCursor(region, m_viMousePos, translate);

  // mouse is over the 3D window
  if (region.isValid() && IsRegion3D(region)) {
    bool bPerformUpdate = false;

    if(clip) {
      bPerformUpdate = MouseMoveClip(m_viMousePos, rotate, translate, region);
    } else {
      bPerformUpdate = MouseMove3D(m_viMousePos, clearview, rotate, translate,
                                   region);
    }

    if (bPerformUpdate) UpdateWindow();
  } else if ( (selectedRegionSplitter != REGION_SPLITTER_NONE) &&
            (event->buttons() & (Qt::LeftButton|Qt::RightButton)) ) {
    FLOATVECTOR2 frac = FLOATVECTOR2(m_viMousePos) / FLOATVECTOR2(m_vWinDim);
    FLOATVECTOR2 winFraction = WindowFraction2x2();
    if (selectedRegionSplitter == REGION_SPLITTER_HORIZONTAL_2x2 ||
        selectedRegionSplitter == REGION_SPLITTER_BOTH_2x2) {
      winFraction.y = frac.y;
    }
    if (selectedRegionSplitter == REGION_SPLITTER_VERTICAL_2x2 ||
        selectedRegionSplitter == REGION_SPLITTER_BOTH_2x2) {
      winFraction.x = frac.x;
    }
    SetWindowFraction2x2(winFraction);
    SetupArcBall();
  }
}

// A mouse movement which should only affect the clip plane.
bool RenderWindow::MouseMoveClip(INTVECTOR2 pos, bool rotate, bool translate,
                                 LuaClassInstance region)
{
  bool bUpdate = false;
  if (rotate) {
    UINTVECTOR2 upos(static_cast<uint32_t>(pos.x),
                     static_cast<uint32_t>(pos.y));
    RegionData *regionData = GetRegionData(region);
    SetClipRotationDelta(region,
                         regionData->clipArcBall.Drag(upos).ComputeRotation(),
                         true, true);
    regionData->clipArcBall.Click(UINTVECTOR2(pos.x, pos.y));
    bUpdate = true;
  }
  if (translate) {
    INTVECTOR2 viPosDelta = m_viMousePos - initialClickPos;
    initialClickPos = m_viMousePos;
    SetClipTranslationDelta(region,
                            FLOATVECTOR3(float(viPosDelta.x*2) / m_vWinDim.x,
                                         float(viPosDelta.y*2) / m_vWinDim.y,
                                         0),
                            true, true);
    bUpdate = true;
  }
  return bUpdate;
}

// Move the mouse by the given amount.  Flags tell which rendering parameters
// should be affected by the mouse movement.
bool RenderWindow::MouseMove3D(INTVECTOR2 pos, bool clearview, bool rotate,
                               bool translate, LuaClassInstance region)
{
  bool bPerformUpdate = false;

  if (m_Renderer->GetRendermode() == AbstrRenderer::RM_ISOSURFACE &&
      m_Renderer->GetCV() && clearview) {
    SetCVFocusPos(region, m_viMousePos);
  }

  if (rotate) {
    UINTVECTOR2 unsigned_pos(pos.x, pos.y);
    RegionData *regionData = GetRegionData(region);
    SetRotationDelta(region,
                     regionData->arcBall.Drag(unsigned_pos).ComputeRotation(),
                     true);

    regionData->arcBall.Click(UINTVECTOR2(pos.x, pos.y));
    bPerformUpdate = true;
  }
  if (translate) {
    INTVECTOR2 viPosDelta = m_viMousePos - initialClickPos;
    initialClickPos = m_viMousePos;
    SetTranslationDelta(region,
                        FLOATVECTOR3(float(viPosDelta.x*2) / m_vWinDim.x,
                                     float(viPosDelta.y*2) / m_vWinDim.y,0),
                        true);
    bPerformUpdate = true;
  }
  return bPerformUpdate;
}

void RenderWindow::WheelEvent(QWheelEvent *event) {
  LuaClassInstance renderRegion = GetRegionUnderCursor(m_viMousePos);
  if (renderRegion.isDefaultInstance())
    return;

  // mouse is over the 3D window
  if (IsRegion3D(renderRegion)) {
    float fZoom = ((m_bInvWheel) ? -1 : 1) * event->delta()/1000.0f;
    MESSAGE("mousewheel click, delta/zoom: %d/%f", event->delta(), fZoom);

    // User can hold control to modify only the clip plane.  Note however that
    // if the plane is locked to the volume, we'll end up translating the plane
    // regardless of whether or not control is held.
    if(event->modifiers() & Qt::ControlModifier) {
      SetClipTranslationDelta(renderRegion,
                              FLOATVECTOR3(fZoom/10.f, fZoom/10.f, 0.f), true, true);
    } else {
      SetTranslationDelta(renderRegion, FLOATVECTOR3(0,0,fZoom), true);
    }
  } else if (IsRegion2D(renderRegion))   {
    int iZoom = event->delta() > 0 ? 1 : event->delta() < 0 ? -1 : 0;
    int iNewSliceDepth =
      std::max<int>(0,
                    static_cast<int>(GetSliceDepth(renderRegion))+iZoom);
    size_t sliceDimension = size_t(GetRegionWindowMode(renderRegion));
    iNewSliceDepth =
      std::min<int>(iNewSliceDepth,
                    m_Renderer->GetDataset().GetDomainSize()[sliceDimension]-1);
    SetSliceDepth(renderRegion, uint64_t(iNewSliceDepth));
  }
  UpdateWindow();
}

LuaClassInstance RenderWindow::GetRegionUnderCursor(INTVECTOR2 vPos) const {
  if (vPos.x < 0 || vPos.y < 0)
      return LuaClassInstance();

  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();

  vPos.y = m_vWinDim.y - vPos.y;
  for (size_t i=0; i < GetActiveRenderRegions().size(); ++i) {
    if (DoesRegionContainPoint(GetActiveRenderRegions()[i], UINTVECTOR2(vPos)))
      return GetActiveRenderRegions()[i];
  }
  return LuaClassInstance();
}

void RenderWindow::UpdateCursor(LuaClassInstance region,
                                INTVECTOR2 pos, bool translate) {
  if (region.isValid() == false) { // We are likely dealing with a splitter
    if (selectedRegionSplitter == REGION_SPLITTER_NONE) { // else cursor already set.
      RegionSplitter hoveredRegionSplitter = GetRegionSplitter(pos);
      switch (hoveredRegionSplitter) {
        case REGION_SPLITTER_HORIZONTAL_2x2:
          GetQtWidget()->setCursor(Qt::SplitVCursor);
          break;
        case REGION_SPLITTER_VERTICAL_2x2:
          GetQtWidget()->setCursor(Qt::SplitHCursor);
          break;
        case REGION_SPLITTER_BOTH_2x2:
          GetQtWidget()->setCursor(Qt::SizeAllCursor);
          break;
        default: ; //Do nothing.
      };
    }
  } else {
    /// @todo Convert to a script call.
    if (translate && IsRegion3D(region))
      GetQtWidget()->setCursor(Qt::ClosedHandCursor);
    else
      GetQtWidget()->unsetCursor();
  }
}

void RenderWindow::KeyPressEvent ( QKeyEvent * event ) {

  LuaClassInstance selectedRegion = GetRegionUnderCursor(m_viMousePos);

  switch (event->key()) {
    case Qt::Key_F :
      ToggleFullscreen();
      break;
    case Qt::Key_C :
      m_Renderer->SetRenderCoordArrows(!m_Renderer->GetRenderCoordArrows());
      break;
    case Qt::Key_T :
      m_Renderer->Transfer3DRotationToMIP();
      break;
    case Qt::Key_P :
      m_Renderer->Set2DPlanesIn3DView(!m_Renderer->Get2DPlanesIn3DView());
      break;
    case Qt::Key_R :
      ResetRenderingParameters();
      break;
    case Qt::Key_S:
      try {
        FLOATVECTOR3 loc = m_Renderer->Pick(UINTVECTOR2(m_viMousePos));
        OTHER("pick location: %g %g %g", loc[0], loc[1], loc[2]);
      } catch(const std::runtime_error& err) {
        T_ERROR("Pick failed: %s", err.what());
      }
      break;
    case Qt::Key_Space : {
      if (selectedRegion.isValid() == false)
        break;

      EViewMode newViewMode = EViewMode((int(GetViewMode()) + 1) % int(VM_INVALID));
      vector<LuaClassInstance> newRenderRegions;

      if (newViewMode == VM_SINGLE) {
        newRenderRegions.push_back(selectedRegion);
      } else {
        if (m_Renderer->GetStereo()) {
          m_Renderer->SetStereo(false);
          EmitStereoDisabled();
        }
        if (newViewMode == VM_TWOBYTWO) {
          for (size_t i=0; i < 4; ++i)
            newRenderRegions.push_back(luaRenderRegions[i][selected2x2Regions[i]]);
        }
      }

      SetViewMode(newRenderRegions, newViewMode);
    }
      break;
    case Qt::Key_X :
      if(selectedRegion.isValid() && IsRegion2D(selectedRegion)) {
        bool flipX = Get2DFlipModeX(selectedRegion);
        flipX = !flipX;
        Set2DFlipMode(selectedRegion, flipX, Get2DFlipModeY(selectedRegion));
      }
      break;
    case Qt::Key_Y :
      if(selectedRegion.isValid() && IsRegion2D(selectedRegion)) {
        bool flipY = Get2DFlipModeY(selectedRegion);
        flipY = !flipY;
        Set2DFlipMode(selectedRegion, Get2DFlipModeX(selectedRegion), flipY);
      }
      break;
    case Qt::Key_M :
      if(selectedRegion.isValid() && IsRegion2D(selectedRegion)) {
        bool useMIP = !GetUseMIP(selectedRegion);
        SetUseMIP(selectedRegion, useMIP);
      }
      break;
    case Qt::Key_A : {
      RegionData *regionData = GetRegionData(selectedRegion);
      regionData->arcBall.SetUseTranslation(
                                      !regionData->arcBall.GetUseTranslation());
    }
      break;
    case Qt::Key_PageDown : case Qt::Key_PageUp :
      if (selectedRegion.isValid() && IsRegion2D(selectedRegion)) {
        const size_t sliceDimension = static_cast<size_t>(GetRegionWindowMode(selectedRegion));
        const int currSlice = static_cast<int>(GetSliceDepth(selectedRegion));
        const int numSlices = m_Renderer->GetDataset().GetDomainSize()[sliceDimension]-1;
        int sliceChange = numSlices / 10;
        if (event->key() == Qt::Key_PageDown)
          sliceChange = -sliceChange;
        int newSliceDepth = MathTools::Clamp(currSlice + sliceChange, 0, numSlices);
        SetSliceDepth(selectedRegion, uint64_t(newSliceDepth));
      }
      else if (selectedRegion.isValid() && IsRegion3D(selectedRegion)) {
        const float zoom = (event->key() == Qt::Key_PageDown) ? 0.01f : -0.01f;
        SetTranslationDelta(selectedRegion, FLOATVECTOR3(0, 0, zoom), true);
      }
      break;
  }
}

void RenderWindow::CloseEvent(QCloseEvent* close) {
  this->GetQtWidget()->setEnabled(false);
  this->GetQtWidget()->lower();
  EmitWindowClosing();
  Cleanup();
  close->accept();
}

void RenderWindow::FocusInEvent ( QFocusEvent * event ) {
  if (m_Renderer) m_Renderer->SetTimeSlice(m_iTimeSliceMSecsActive);

  if (event->gotFocus()) EmitWindowActive();
}

void RenderWindow::FocusOutEvent ( QFocusEvent * event ) {
  if (m_Renderer) m_Renderer->SetTimeSlice(m_iTimeSliceMSecsInActive);
  if (!event->gotFocus()) EmitWindowInActive();
}

void RenderWindow::SetupArcBall() {
  for (size_t i=0; i < GetActiveRenderRegions().size(); ++i) {
    LuaClassInstance region = GetActiveRenderRegions()[i];
    RegionData* regionData = GetRegionData(region);

    const UINTVECTOR2 regMin = GetRegionMinCoord(region);
    const UINTVECTOR2 regMax = GetRegionMaxCoord(region);
    const UINTVECTOR2 offset(regMin.x, m_vWinDim.y - regMax.y);
    const UINTVECTOR2 size = regMax - regMin;

    regionData->arcBall.SetWindowOffset(offset.x, offset.y);
    regionData->clipArcBall.SetWindowOffset(offset.x, offset.y);
    regionData->arcBall.SetWindowSize(size.x, size.y);
    regionData->clipArcBall.SetWindowSize(size.x, size.y);
  }
}

void RenderWindow::SetWindowFraction2x2(FLOATVECTOR2 f) {
  f.x = MathTools::Clamp(f.x, 0.0, 1.0);
  f.y = MathTools::Clamp(f.y, 0.0, 1.0);

  m_vWinFraction = f;
  m_Renderer->ScheduleCompleteRedraw();
  UpdateWindowFraction();
}


void RenderWindow::UpdateWindowFraction() {
  if (GetActiveRenderRegions().size() != 4) {
    return; // something is wrong, should be 4...
  }

  const int halfWidth = regionSplitterWidth/2;

  int verticalSplit = static_cast<int>(m_vWinDim.x*m_vWinFraction.x);
  int horizontalSplit = static_cast<int>(m_vWinDim.y*(1-m_vWinFraction.y));

  // Make sure none of the regions are out of bounds.  This can happen
  // since we add/subtract the divider width.
  if (verticalSplit - halfWidth < 0)
    verticalSplit = halfWidth;
  if (verticalSplit + halfWidth > static_cast<int>(m_vWinDim.x))
    verticalSplit = m_vWinDim.x - halfWidth;

  if (horizontalSplit - halfWidth < 0)
    horizontalSplit = halfWidth;
  if (horizontalSplit + halfWidth > static_cast<int>(m_vWinDim.y))
    horizontalSplit = m_vWinDim.y - halfWidth;

  const std::vector<LuaClassInstance> activeRenderRegions =
      GetActiveRenderRegions();

  SetRegionMinCoord(activeRenderRegions[0],
                    UINTVECTOR2(0, horizontalSplit+halfWidth));
  SetRegionMaxCoord(activeRenderRegions[0],
                    UINTVECTOR2(verticalSplit-halfWidth, m_vWinDim.y));

  SetRegionMinCoord(activeRenderRegions[1],
                    UINTVECTOR2(verticalSplit+halfWidth,
                                horizontalSplit+halfWidth));
  SetRegionMaxCoord(activeRenderRegions[1],
                    UINTVECTOR2(m_vWinDim.x, m_vWinDim.y));

  SetRegionMinCoord(activeRenderRegions[2],
                    UINTVECTOR2(0,0));
  SetRegionMaxCoord(activeRenderRegions[2],
                    UINTVECTOR2(verticalSplit-halfWidth,
                                horizontalSplit-halfWidth));

  SetRegionMinCoord(activeRenderRegions[3],
                    UINTVECTOR2(verticalSplit+halfWidth, 0));
  SetRegionMaxCoord(activeRenderRegions[3],
                    UINTVECTOR2(m_vWinDim.x, horizontalSplit-halfWidth));
}

static std::string view_mode(RenderWindow::EViewMode mode) {
  switch(mode) {
    case RenderWindow::VM_SINGLE: return "single"; break;
    case RenderWindow::VM_TWOBYTWO: return "two-by-two"; break;
    case RenderWindow::VM_INVALID: /* fall-through */
    default: return "invalid"; break;
  }
}

LuaClassInstance
RenderWindow::GetFirst3DRegion() {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  return ss->cexecRet<LuaClassInstance>(GetLuaAbstrRenderer().fqName()
                                        + ".getFirst3DRenderRegion");
}

const std::vector<LuaClassInstance>
RenderWindow::GetActiveRenderRegions() const {
  return m_MasterController.LuaScript()->
      cexecRet<std::vector<LuaClassInstance> >(m_LuaAbstrRenderer.fqName() +
                                               ".getRenderRegions");
}

void RenderWindow::SetActiveRenderRegions(std::vector<LuaClassInstance> regions)
  const
{
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  ss->cexec(GetLuaAbstrRenderer().fqName() + ".setRenderRegions",
            regions);
}

void RenderWindow::ToggleRenderWindowView2x2() {
  std::vector<LuaClassInstance> newRenderRegions;
  if (GetActiveRenderRegions().size() == 4)
    newRenderRegions = GetActiveRenderRegions();
  else {
    //Just use the default 4 regions.
    for (size_t i=0; i < 4; ++i)
      newRenderRegions.push_back(luaRenderRegions[i][selected2x2Regions[i]]);
  }
  SetViewMode(newRenderRegions, VM_TWOBYTWO);
}


bool RenderWindow::SetRenderWindowView3D() {
  std::vector<LuaClassInstance> newRenderRegions;

  for (int i=0; i < MAX_RENDER_REGIONS; ++i) {
    for (int j=0; j < NUM_WINDOW_MODES; ++j) {
      if (IsRegion3D(luaRenderRegions[i][j])) {
        newRenderRegions.push_back(luaRenderRegions[i][j]);
        SetViewMode(newRenderRegions, RenderWindow::VM_SINGLE);
        return true;
      }
    }
  }
  return false;
}

void RenderWindow::ToggleRenderWindowViewSingle() {
  std::vector<LuaClassInstance> newRenderRegions;
  if (!GetActiveRenderRegions().empty())
    newRenderRegions.push_back(GetActiveRenderRegions()[0]);
  else
    newRenderRegions.push_back(luaRenderRegions[0][selected2x2Regions[0]]);
  SetViewMode(newRenderRegions, VM_SINGLE);
}

void
RenderWindow::SetViewMode(const std::vector<LuaClassInstance> &newRenderRegions,
                          EViewMode eViewMode)
{
  m_eViewMode = eViewMode;

  if (eViewMode == VM_SINGLE) {
    if (newRenderRegions.size() != 1) {
      T_ERROR("VM_SINGLE view mode expected only a single RenderRegion, not %d.",
              newRenderRegions.size());
    }
    SetActiveRenderRegions(newRenderRegions);

    tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
    LuaClassInstance firstRenRegion = GetActiveRenderRegions()[0];
    RenderRegion* regPtr = firstRenRegion.getRawPointer<RenderRegion>(ss);

    /// @fixme Is the following code correct? At the top of RenderRegion.h it
    /// says:
    // NOTE: client code should never directly modify a RenderRegion. Instead,
    // modifications should be done through the tuvok API so that tuvok is aware
    // of these changes.
    regPtr->minCoord = UINTVECTOR2(0,0);
    regPtr->maxCoord = m_vWinDim;

  } else if (eViewMode == VM_TWOBYTWO) {
    if (newRenderRegions.size() != 4) {
      T_ERROR("VM_TWOBYTWO view mode expected 4 RenderRegions, not %d.",
              newRenderRegions.size());
    }
    SetActiveRenderRegions(newRenderRegions);
    UpdateWindowFraction();
  }

  SetupArcBall();
  m_Renderer->ScheduleCompleteRedraw();
  UpdateWindow();
  EmitRenderWindowViewChanged(int(GetViewMode()));

  Controller::Instance().Provenance("vmode", "viewmode",
                                    view_mode(eViewMode));
}

void RenderWindow::Initialize() {
  // Note that we create the RenderRegions here and not directly in the constructor
  // because we first need the dataset to be loaded so that we can setup the
  // initial slice index.

  // NOTE: Since this function is called from our derived class' constructor
  // we can generate lua instances and have them associated with the call
  // to the constructor (ensures we do not have to hit undo multiple times
  // in order to undo the creation of the render window).

  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();

  for (int i=0; i < MAX_RENDER_REGIONS; ++i) {
    luaRenderRegions[i][0] = ss->cexecRet<LuaClassInstance>(
        "tuvok.renderRegion3D.new");

    int mode = static_cast<int>(RenderRegion::WM_SAGITTAL);
    uint64_t sliceIndex = m_Renderer->GetDataset().GetDomainSize()[mode]/2;
    luaRenderRegions[i][1] = ss->cexecRet<LuaClassInstance>(
            "tuvok.renderRegion2D.new",
            static_cast<RenderRegion::EWindowMode>(mode),
            sliceIndex);

    mode = static_cast<int>(RenderRegion::WM_AXIAL);
    sliceIndex = m_Renderer->GetDataset().GetDomainSize()[mode]/2;
    luaRenderRegions[i][2] = ss->cexecRet<LuaClassInstance>(
            "tuvok.renderRegion2D.new",
            static_cast<RenderRegion::EWindowMode>(mode),
            sliceIndex);

    mode = static_cast<int>(RenderRegion::WM_CORONAL);
    sliceIndex = m_Renderer->GetDataset().GetDomainSize()[mode]/2;
    luaRenderRegions[i][3] = ss->cexecRet<LuaClassInstance>(
            "tuvok.renderRegion2D.new",
            static_cast<RenderRegion::EWindowMode>(mode),
            sliceIndex);
  }

  for (int i=0; i < 4; ++i)
    selected2x2Regions[i] = i;

  std::vector<LuaClassInstance> initialRenderRegions;
  initialRenderRegions.push_back(luaRenderRegions[0][0]);
  ss->cexec(GetLuaAbstrRenderer().fqName() + ".setRenderRegions",
            initialRenderRegions);

  // initialize region data map now that we have all the render regions
  for (int i=0; i < MAX_RENDER_REGIONS; ++i)
    for (int j=0; j < NUM_WINDOW_MODES; ++j)
      regionDataMap.insert(std::make_pair(
          luaRenderRegions[i][j].getGlobalInstID(),
          &regionDatas[i][j]));

  SetupArcBall();
}

void RenderWindow::Cleanup() {
  if (m_Renderer == NULL || !m_bRenderSubsysOK) return;

  m_Renderer->Cleanup();
  // ReleaseVoumeRenderer will call classDelete on our abstract renderer.
  m_MasterController.ReleaseVolumeRenderer(m_Renderer);
  m_Renderer = NULL;

  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();

  for (int i=0; i < MAX_RENDER_REGIONS; ++i)
    for (int j=0; j < NUM_WINDOW_MODES; ++j)
      ss->cexec("deleteClass", luaRenderRegions[i][j]);
}

void RenderWindow::CheckForRedraw() {
  if (m_Renderer && m_Renderer->CheckForRedraw()) {
    UpdateWindow();
  }
}

AbstrRenderer::ERenderMode RenderWindow::GetRenderMode() const {
  return m_Renderer->GetRendermode();
}

void RenderWindow::SetBlendPrecision(
    AbstrRenderer::EBlendPrecision eBlendPrecisionMode) {
  m_MasterController.LuaScript()->cexec(
      GetLuaAbstrRenderer().fqName() + ".setBlendPrecision",
      eBlendPrecisionMode);
}

void RenderWindow::SetPerfMeasures(unsigned int iMinFramerate,
                                   bool bRenderLowResIntermediateResults,
                                   float fScreenResDecFactor,
                                   float fSampleDecFactor,
                                   unsigned int iLODDelay,
                                   unsigned int iActiveTS,
                                   unsigned int iInactiveTS) {
  m_iTimeSliceMSecsActive   = iActiveTS;
  m_iTimeSliceMSecsInActive = iInactiveTS;
  m_Renderer->SetPerfMeasures(iMinFramerate, bRenderLowResIntermediateResults,
                              fScreenResDecFactor,
                              fSampleDecFactor, iLODDelay);
}

bool RenderWindow::CaptureFrame(const std::string& strFilename,
                                bool bPreserveTransparency)
{
  GLFrameCapture f;
  AbstrRenderer::ERendererTarget mode = m_Renderer->GetRendererTarget();
  FLOATVECTOR3 color[2] = {m_Renderer->GetBackgroundColor(0),
                           m_Renderer->GetBackgroundColor(1)};
  FLOATVECTOR3 black[2] = {FLOATVECTOR3(0,0,0), FLOATVECTOR3(0,0,0)};
  if (bPreserveTransparency) m_Renderer->SetBackgroundColors(black[0],black[1]);

  m_Renderer->SetRendererTarget(AbstrRenderer::RT_CAPTURE);
  while(m_Renderer->CheckForRedraw()) {
    QCoreApplication::processEvents();
    PaintRenderer();
  }
  // as the window is double buffered call repaint twice
  ForceRepaint();  ForceRepaint();

  bool rv = f.CaptureSingleFrame(strFilename, bPreserveTransparency);
  m_Renderer->SetRendererTarget(mode);
  if (bPreserveTransparency) m_Renderer->SetBackgroundColors(color[0],color[1]);
  return rv;
}


bool RenderWindow::CaptureMIPFrame(const std::string& strFilename, float fAngle,
                                   bool bOrtho, bool bFinalFrame, bool bUseLOD,
                                   bool bPreserveTransparency,
                                   std::string* strRealFilename)
{
  GLFrameCapture f;
  m_Renderer->SetMIPRotationAngle(fAngle);
  bool bSystemOrtho = m_Renderer->GetOrthoView();
  if (bSystemOrtho != bOrtho) m_Renderer->SetOrthoView(bOrtho);
  m_Renderer->SetMIPLOD(bUseLOD);
  if (bFinalFrame) { // restore state
    m_Renderer->SetMIPRotationAngle(0.0f);
    if (bSystemOrtho != bOrtho) m_Renderer->SetOrthoView(bSystemOrtho);
  }
  // as the window is double buffered call repaint twice
  ForceRepaint();  ForceRepaint();

  std::string strSequenceName = SysTools::FindNextSequenceName(strFilename);
  if (strRealFilename) (*strRealFilename) = strSequenceName;
  return f.CaptureSingleFrame(strSequenceName, bPreserveTransparency);
}

bool RenderWindow::CaptureSequenceFrame(const std::string& strFilename,
                                        bool bPreserveTransparency,
                                        std::string* strRealFilename)
{
  std::string strSequenceName = SysTools::FindNextSequenceName(strFilename);
  if (strRealFilename) (*strRealFilename) = strSequenceName;
  return CaptureFrame(strSequenceName, bPreserveTransparency); 
}

void RenderWindow::SetTranslation(LuaClassInstance renderRegion,
                                  FLOATMATRIX4 accumulatedTranslation,
                                  bool logProvenance) {
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  string arn = GetLuaAbstrRenderer().fqName();
  if (!logProvenance) ss->setTempProvDisable(true);
  ss->cexec(arn + ".setRegionTranslation4x4", renderRegion,
           accumulatedTranslation);
  if (!logProvenance) ss->setTempProvDisable(false);

  RegionData *regionData = GetRegionData(renderRegion);
  regionData->arcBall.SetTranslation(accumulatedTranslation);

  if(m_Renderer->ClipPlaneLocked()) {
    // We want to translate the plane to the *dataset's* origin before rotating,
    // not the origin of the entire 3D domain!  The difference is particularly
    // relevant when the clip plane is outside the dataset's domain: the `center'
    // of the plane (*cough*) should rotate about the dataset, not about the
    // plane itself.
    FLOATMATRIX4 from_pt_to_0, from_0_to_pt;
    FLOATMATRIX4 regTrans = GetTranslation(renderRegion);
    from_pt_to_0.Translation(-regTrans.m41,
                             -regTrans.m42,
                             -regTrans.m43);
    from_0_to_pt.Translation(regTrans.m41,
                             regTrans.m42,
                             regTrans.m43);

    m_ClipPlane.Default(false);
    m_ClipPlane.Transform(regTrans
                          * from_pt_to_0 * regionData->clipRotation[0] *
                          from_0_to_pt, false);
    SetClipPlane(renderRegion, m_ClipPlane);
  }

  Controller::Instance().Provenance("translation", "translate");
}

void RenderWindow::SetTranslationDelta(LuaClassInstance renderRegion,
                                       const FLOATVECTOR3& trans, bool
                                       bPropagate) {
  FLOATMATRIX4 newTranslation = GetTranslation(renderRegion);
  newTranslation.m41 += trans.x;
  newTranslation.m42 -= trans.y;
  newTranslation.m43 += trans.z;
  SetTranslation(renderRegion, newTranslation, false);
  RegionData *regionData = GetRegionData(renderRegion);
  regionData->arcBall.SetTranslation(newTranslation);

  if(GetRenderer()->ClipPlaneLocked()) {
    // We can't use SetClipTranslationDelta, because it forces the clip plane
    // to stay locked along its own normal.  We're not necessarily translating
    // along the clip's normal when doing a regular translation (in fact, it's
    // almost inconceivable that we would be), so we need to do the translation
    // ourself.
    FLOATMATRIX4 translation;
    translation.Translation(trans.x, -trans.y, trans.z);
    m_ClipPlane.Transform(translation, false);
    SetClipPlane(renderRegion, m_ClipPlane);
  }

  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[0].size();i++) {
      LuaClassInstance otherRegion = GetCorrespondingRenderRegion(
          m_vpLocks[0][i], renderRegion);
      if (m_bAbsoluteViewLock)
        m_vpLocks[0][i]->SetTranslation(otherRegion, newTranslation, false);
      else
        m_vpLocks[0][i]->SetTranslationDelta(otherRegion, trans, false);
    }
  }
}

void RenderWindow::FinalizeRotation(LuaClassInstance region, bool bPropagate) {
  // Reset the clip matrix we'll apply; the state is already stored/applied in
  // the ExtendedPlane instance.
  RegionData* regionData = GetRegionData(region);
  regionData->clipRotation[0] = FLOATMATRIX4();
  regionData->clipRotation[1] = FLOATMATRIX4();
  if (bPropagate) {
    for (size_t i = 0;i<m_vpLocks[0].size();i++) {
      LuaClassInstance otherRegion = GetCorrespondingRenderRegion(
          m_vpLocks[0][i], region);
      m_vpLocks[0][i]->FinalizeRotation(otherRegion, false);
    }
  }
  // Call SetRotation with logProvenance = true.
  SetRotation(region, GetRotation(region), true);
}

void RenderWindow::SetRotation(LuaClassInstance region,
                               FLOATMATRIX4 newRotation,
                               bool logProvenance) {

  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();
  string arn = GetLuaAbstrRenderer().fqName();

  // Temporarily disable provenance. We don't want to record every single
  // rotation command, only the final rotation command.
  if (!logProvenance) ss->setTempProvDisable(true);
  ss->cexec(arn + ".setRegionRotation4x4", region, newRotation);
  if (!logProvenance) ss->setTempProvDisable(false);

  if(m_Renderer->ClipPlaneLocked()) {
    
    FLOATMATRIX4 from_pt_to_0, from_0_to_pt;

    // We want to translate the plane to the *dataset's* origin before rotating,
    // not the origin of the entire 3D domain!  The difference is particularly
    // relevant when the clip plane is outside the dataset's domain: the `center'
    // of the plane (*cough*) should rotate about the dataset, not about the
    // plane itself.
    FLOATMATRIX4 regTrans = GetTranslation(region);
    from_pt_to_0.Translation(-regTrans.m41,
                             -regTrans.m42,
                             -regTrans.m43);
    from_0_to_pt.Translation(regTrans.m41,
                             regTrans.m42,
                             regTrans.m43);
  
    RegionData* regionData = GetRegionData(region);
    regionData->clipRotation[0] = newRotation;
    m_ClipPlane.Default(false);
    m_ClipPlane.Transform(GetTranslation(region) * from_pt_to_0 * regionData->clipRotation[0] * from_0_to_pt, false);
    SetClipPlane(region, m_ClipPlane);
  }
}

void RenderWindow::SetRotationDelta(LuaClassInstance region,
                                    const FLOATMATRIX4& rotDelta,
                                    bool bPropagate) {
  const FLOATMATRIX4 newRotation = GetRotation(region) * rotDelta;
  SetRotation(region, newRotation, false);

  if(m_Renderer->ClipPlaneLocked()) {
    SetClipRotationDelta(region, rotDelta, bPropagate, false);
  }

  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[0].size();i++) {
      LuaClassInstance otherRegion = GetCorrespondingRenderRegion(
          m_vpLocks[0][i], region);

      if (m_bAbsoluteViewLock)
        m_vpLocks[0][i]->SetRotation(otherRegion, newRotation, false);
      else
        m_vpLocks[0][i]->SetRotationDelta(otherRegion, rotDelta, false);
    }
  }
}

void RenderWindow::SetPlaneAtClick(const ExtendedPlane& plane, bool propagate) {
  m_PlaneAtClick = plane;

  if (propagate) {
    for (size_t i = 0;i<m_vpLocks[0].size();i++) {
      m_vpLocks[0][i]->SetPlaneAtClick(m_vpLocks[0][i]->m_ClipPlane, false);
    }
  }
}

void RenderWindow::SetClipPlane(LuaClassInstance renderRegion,
                                const ExtendedPlane &p) {
  /// @fixme I punted and did not add the ExtendedPlane type to
  /// LuaTuvokTypes.h (some of the data is private).
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  m_ClipPlane = p;
  m_Renderer->SetClipPlane(renderRegion.getRawPointer<RenderRegion>(ss),
                           m_ClipPlane);
}

// Applies the given rotation matrix to the clip plane.
// Basically, we're going to translate the plane back to the origin, do the
// rotation, and then push the plane back out to where it should be.  This
// avoids any sort of issues w.r.t. rotating about the wrong point.
void RenderWindow::SetClipRotationDelta(LuaClassInstance renderRegion,
                                        const FLOATMATRIX4& rotDelta,
                                        bool bPropagate,
                                        bool bSecondary)
{
  RegionData* regionData = GetRegionData(renderRegion);

  regionData->clipRotation[bSecondary ? 1 : 0] = regionData->clipRotation[bSecondary ? 1 : 0] * rotDelta;
 
  FLOATMATRIX4 from_pt_to_0, from_0_to_pt;

  // We want to translate the plane to the *dataset's* origin before rotating,
  // not the origin of the entire 3D domain!  The difference is particularly
  // relevant when the clip plane is outside the dataset's domain: the `center'
  // of the plane (*cough*) should rotate about the dataset, not about the
  // plane itself.
  FLOATMATRIX4 regTrans = GetTranslation(renderRegion);
  from_pt_to_0.Translation(-regTrans.m41,
                           -regTrans.m42,
                           -regTrans.m43);
  from_0_to_pt.Translation(regTrans.m41,
                           regTrans.m42,
                           regTrans.m43);

  ExtendedPlane rotated = m_PlaneAtClick;
  rotated.Transform(from_pt_to_0 *
                    regionData->clipRotation[bSecondary ? 1 : 0] *
                    from_0_to_pt, bSecondary);
  SetClipPlane(renderRegion, rotated);

  if (bPropagate) {
    for(std::vector<RenderWindow*>::const_iterator iter = m_vpLocks[0].begin();
        iter != m_vpLocks[0].end(); ++iter) {
      LuaClassInstance otherRegion = GetCorrespondingRenderRegion(
          *iter,renderRegion);

      if (m_bAbsoluteViewLock) {
        (*iter)->SetClipPlane(otherRegion, m_ClipPlane);
      } else {
        (*iter)->SetClipRotationDelta(otherRegion, rotDelta, false, bSecondary);
      }
    }
  }
}

// Translates the clip plane by the given vector, projected along the clip
// plane's normal.
void RenderWindow::SetClipTranslationDelta(LuaClassInstance renderRegion,
                                           const FLOATVECTOR3 &trans,
                                           bool bPropagate,
                                           bool bSecondary)
{
  FLOATMATRIX4 translation;

  // Get the scalar projection of the user's translation along the clip plane's
  // normal.
  float sproj = trans ^ m_ClipPlane.Plane().xyz();
  // The actual translation is along the clip's normal, weighted by the user's
  // translation.
  FLOATVECTOR3 tr = sproj * m_ClipPlane.Plane().xyz();
  translation.Translation(tr.x, tr.y, tr.z);

  ExtendedPlane translated = m_ClipPlane;
  translated.Transform(translation, bSecondary);
  SetClipPlane(renderRegion, translated);

  if (bPropagate) {
    for(std::vector<RenderWindow*>::iterator iter = m_vpLocks[0].begin();
        iter != m_vpLocks[0].end(); ++iter) {

      LuaClassInstance otherRegion = GetCorrespondingRenderRegion(*iter, renderRegion);

      if (m_bAbsoluteViewLock) {
        (*iter)->SetClipPlane(otherRegion, m_ClipPlane);
      } else {
        (*iter)->SetClipTranslationDelta(otherRegion, trans, bPropagate, bSecondary);
      }
    }
  }
}

LuaClassInstance
RenderWindow::GetCorrespondingRenderRegion(const RenderWindow* otherRW,
                                           LuaClassInstance myRR) const {
  for (int i=0; i < MAX_RENDER_REGIONS; ++i)
    for (int j=0; j < NUM_WINDOW_MODES; ++j)
      if (luaRenderRegions[i][j].getGlobalInstID() == myRR.getGlobalInstID())
        return otherRW->luaRenderRegions[i][j];

  // This should always succeed since myRR must be in this->renderRegions.
  assert(false);
  return LuaClassInstance();
}

void RenderWindow::CloneViewState(RenderWindow* other) {
  m_mAccumulatedClipTranslation = other->m_mAccumulatedClipTranslation;

  for (int i=0; i < MAX_RENDER_REGIONS; ++i)
    for (int j=0; j < NUM_WINDOW_MODES; ++j) {
      const LuaClassInstance otherRegion = other->luaRenderRegions[i][j];
      const RegionData *otherData = other->GetRegionData(otherRegion);
      RegionData *data = GetRegionData(luaRenderRegions[i][j]);
      *data = *otherData;

      SetRotation(luaRenderRegions[i][j], other->GetRotation(otherRegion), true);
      SetTranslation(luaRenderRegions[i][j],
                     other->GetTranslation(otherRegion), true);
    }
}

void RenderWindow::CloneRendermode(RenderWindow* other) {
  SetRendermode(other->GetRenderMode());

  m_Renderer->SetUseLighting(other->m_Renderer->GetUseLighting());
  m_Renderer->SetSampleRateModifier(other->m_Renderer->GetSampleRateModifier());
  m_Renderer->SetGlobalBBox(other->m_Renderer->GetGlobalBBox());
  m_Renderer->SetLocalBBox(other->m_Renderer->GetLocalBBox());

  if (other->m_Renderer->IsClipPlaneEnabled())
    m_Renderer->EnableClipPlane();
  else
    m_Renderer->DisableClipPlane();

  m_Renderer->SetIsosufaceColor(other->m_Renderer->GetIsosufaceColor());
  m_Renderer->SetIsoValue(other->m_Renderer->GetIsoValue());
  m_Renderer->SetCVIsoValue(other->m_Renderer->GetCVIsoValue());
  m_Renderer->SetCVSize(other->m_Renderer->GetCVSize());
  m_Renderer->SetCVContextScale(other->m_Renderer->GetCVContextScale());
  m_Renderer->SetCVBorderScale(other->m_Renderer->GetCVBorderScale());
  m_Renderer->SetCVColor(other->m_Renderer->GetCVColor());
  m_Renderer->SetCV(other->m_Renderer->GetCV());
  m_Renderer->SetCVFocusPos(other->m_Renderer->GetCVFocusPos());
  m_Renderer->SetInterpolant(other->m_Renderer->GetInterpolant());
}

void RenderWindow::SetRendermode(AbstrRenderer::ERenderMode eRenderMode, bool bPropagate) {
  m_Renderer->SetRendermode(eRenderMode);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetRendermode(eRenderMode, false);
    }
  }
}

void RenderWindow::SetColors(FLOATVECTOR3 vTopColor, FLOATVECTOR3 vBotColor,
                             FLOATVECTOR4 vTextColor) {
  tr1::shared_ptr<LuaScripting> ss = m_MasterController.LuaScript();
  string abstrRenName = GetLuaAbstrRenderer().fqName();

  /// @todo Composite these two calls into one lua function to ensure they occur
  ///       on the same undo/redo call.
  ss->cexec(abstrRenName + ".setBGColors", vTopColor, vBotColor);
  ss->cexec(abstrRenName + ".setTextColor", vTextColor);
}

void RenderWindow::SetUseLighting(bool bLighting, bool bPropagate) {
  m_Renderer->SetUseLighting(bLighting);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetUseLighting(bLighting, false);
    }
  }
}

bool RenderWindow::GetUseLighting() const {
  return m_Renderer->GetUseLighting();
}

void RenderWindow::SetSampleRateModifier(float fSampleRateModifier, bool bPropagate) {
  m_Renderer->SetSampleRateModifier(fSampleRateModifier);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetSampleRateModifier(fSampleRateModifier, false);
    }
  }
}

void RenderWindow::SetIsoValue(float fIsoVal, bool bPropagate) {
  m_Renderer->SetIsoValue(fIsoVal);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetIsoValue(fIsoVal, false);
    }
  }
}

void RenderWindow::SetCVIsoValue(float fIsoVal, bool bPropagate) {
  m_Renderer->SetCVIsoValue(fIsoVal);
  if (bPropagate) {
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetCVIsoValue(fIsoVal, false);
    }
  }
}

void RenderWindow::SetCVSize(float fSize, bool bPropagate) {
  m_Renderer->SetCVSize(fSize);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetCVSize(fSize, false);
    }
  }
}

void RenderWindow::SetCVContextScale(float fScale, bool bPropagate) {
  m_Renderer->SetCVContextScale(fScale);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetCVContextScale(fScale, false);
    }
  }
}

void RenderWindow::SetCVBorderScale(float fScale, bool bPropagate) {
  m_Renderer->SetCVBorderScale(fScale);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetCVBorderScale(fScale, false);
    }
  }
}

void RenderWindow::SetGlobalBBox(bool bRenderBBox, bool bPropagate) {
  m_Renderer->SetGlobalBBox(bRenderBBox);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetGlobalBBox(bRenderBBox, false);
    }
  }
}

void RenderWindow::SetLocalBBox(bool bRenderBBox, bool bPropagate) {
  m_Renderer->SetLocalBBox(bRenderBBox);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetLocalBBox(bRenderBBox, false);
    }
  }
}
void RenderWindow::SetClipPlaneEnabled(bool enable, bool bPropagate)
{
  if(enable) {
    m_Renderer->EnableClipPlane();
    // Restore the locking setting which was active when the clip plane was
    // disabled.
    SetClipPlaneRelativeLock(m_SavedClipLocked, bPropagate);
  } else {
    // Disable the clip plane, and then implicitly lock it to the volume.  This
    // means that the clip plane will appear to `follow' the volume while it is
    // disabled, which is a bit unintuitive in some sense.
    // However, it might occur that interactions that happen while the clip
    // plane is disabled could cause it to clip the entire volume when
    // re-enabled, which is *very* confusing.  By keeping it locked while
    // disabled, this is prevented, so it's the lesser of the two evils.
    m_SavedClipLocked = m_Renderer->ClipPlaneLocked();
    m_Renderer->DisableClipPlane();
    SetClipPlaneRelativeLock(true, bPropagate);
  }

  if(bPropagate) {
    for(std::vector<RenderWindow*>::iterator locks = m_vpLocks[1].begin();
        locks != m_vpLocks[1].end(); ++locks) {
      (*locks)->SetClipPlaneEnabled(enable, false);
    }
  }
}

void RenderWindow::SetClipPlaneDisplayed(bool bDisp, bool bPropagate)
{
  m_Renderer->ShowClipPlane(bDisp);
  if(bPropagate) {
    for(std::vector<RenderWindow*>::iterator locks = m_vpLocks[1].begin();
        locks != m_vpLocks[1].end(); ++locks) {
      (*locks)->SetClipPlaneDisplayed(bDisp, false);
    }
  }
}

void RenderWindow::SetClipPlaneRelativeLock(bool bLock, bool bPropagate)
{
  m_Renderer->ClipPlaneRelativeLock(bLock);
  if(bPropagate) {
    for(std::vector<RenderWindow*>::iterator locks = m_vpLocks[1].begin();
        locks != m_vpLocks[1].end(); ++locks) {
      (*locks)->SetClipPlaneRelativeLock(bLock, false);
    }
  }
}

void RenderWindow::SetIsosufaceColor(const FLOATVECTOR3& vIsoColor, bool bPropagate) {
  m_Renderer->SetIsosufaceColor(vIsoColor);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetIsosufaceColor(vIsoColor, false);
    }
  }
}

void RenderWindow::SetCVColor(const FLOATVECTOR3& vIsoColor, bool bPropagate) {
  m_Renderer->SetCVColor(vIsoColor);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetCVColor(vIsoColor, false);
    }
  }
}

void RenderWindow::SetCV(bool bDoClearView, bool bPropagate) {
  m_Renderer->SetCV(bDoClearView);
  if (bPropagate){
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetCV(bDoClearView, false);
    }
  }
}

void RenderWindow::SetCVFocusPos(LuaClassInstance region,
                                 const INTVECTOR2& viMousePos,
                                 bool bPropagate) {
  /// @fixme Expose SetCVFocusPos through the scripting system.
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  m_Renderer->SetCVFocusPos(*region.getRawPointer<RenderRegion>(ss), viMousePos);
  if (bPropagate) {
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetCVFocusPos(region, viMousePos, false);
    }
  }
}

void RenderWindow::SetTimestep(size_t t, bool propagate)
{
  m_Renderer->Timestep(t);
  if(propagate) {
    for (size_t i = 0;i<m_vpLocks[1].size();i++) {
      m_vpLocks[1][i]->SetTimestep(t, false);
    }
  }
}

void RenderWindow::SetLogoParams(QString strLogoFilename, int iLogoPos) {
  m_MasterController.LuaScript()->cexec(
      GetLuaAbstrRenderer().fqName() + ".setLogoParams",
      std::string(strLogoFilename.toAscii()),
      iLogoPos);
}

void RenderWindow::SetAbsoluteViewLock(bool bAbsoluteViewLock) {
  m_bAbsoluteViewLock = bAbsoluteViewLock;
}

pair<double,double> RenderWindow::GetDynamicRange() const {
  pair<double,double> range = m_Renderer->GetDataset().GetRange();

  // Old UVFs lack a maxmin data block, && will say the min > the max.
  if (range.first > range.second)
    return make_pair(0,double(m_Renderer->Get1DTrans()->GetSize()));
  else
    return range;
}

FLOATVECTOR3 RenderWindow::GetIsosufaceColor() const {
  return m_Renderer->GetIsosufaceColor();
}

FLOATVECTOR3 RenderWindow::GetCVColor() const {
  return m_Renderer->GetCVColor();
}

void RenderWindow::ResizeRenderer(int width, int height)
{
  m_vWinDim = UINTVECTOR2((unsigned int)width, (unsigned int)height);

  /// @fixme Create a setMaxCoord function for the region.
  tr1::shared_ptr<LuaScripting> ss(m_MasterController.LuaScript());
  LuaClassInstance firstRenRegion = GetActiveRenderRegions()[0];
  RenderRegion* regPtr = firstRenRegion.getRawPointer<RenderRegion>(ss);

  if (m_Renderer != NULL && m_bRenderSubsysOK) {
    switch (GetViewMode()) {
      case VM_SINGLE :
        regPtr->maxCoord = m_vWinDim;
        break;
      case VM_TWOBYTWO :
        UpdateWindowFraction();
        break;
      default: break; //nothing to do...
    };

    m_Renderer->Resize(UINTVECTOR2(width, height));
    SetupArcBall();
    std::ostringstream wsize;
    wsize << m_vWinDim[0] << " " << m_vWinDim[1] << std::ends;
    Controller::Instance().Provenance("window", "resize", wsize.str());
  }
}

void RenderWindow::PaintRenderer()
{
  if (!m_strMultiRenderGuard.tryLock()) {
    MESSAGE("Rejecting dublicate Paint call");
    return;
  }

  if (m_Renderer != NULL && m_bRenderSubsysOK) {
    if (!m_Renderer->Paint()) {
      static bool bBugUseronlyOnce = true;
      if (bBugUseronlyOnce) {

        if (m_eRendererType == MasterController::OPENGL_2DSBVR) {
          QMessageBox::critical(NULL, "Render error",
                             "The render subsystem is unable to draw the volume"
                             "This normally means ImageVis3D does not support "
                             "your GPU. Please check the debug log "
                             "('Help | Debug Window') for "
                             "errors, and/or use 'Help | Report an Issue' to "
                             "notify the ImageVis3D developers.");      
        } else {
          QMessageBox::critical(NULL, "Render error",
                             "The render subsystem is unable to draw the volume"
                             "This normally means that your driver is "
                             "reporting invalid information about your GPU."
                             "Try switching the renderer into 2D slicing "
                             "mode in the Preferences/Settings.");
        }
        bBugUseronlyOnce = false;
      }
      T_ERROR("m_Renderer->Paint() call failed.");
    }

    if (GetQtWidget()->isActiveWindow()) {
      unsigned int iLevelCount        = m_Renderer->GetCurrentSubFrameCount();
      unsigned int iWorkingLevelCount = m_Renderer->GetWorkingSubFrame();

      unsigned int iBrickCount        = m_Renderer->GetCurrentBrickCount();
      unsigned int iWorkingBrick      = m_Renderer->GetWorkingBrick();

      unsigned int iMinLODIndex       = m_Renderer->GetMinLODIndex();

      m_MainWindow->SetRenderProgressAnUpdateInfo(iLevelCount,
        iWorkingLevelCount, iBrickCount, iWorkingBrick, iMinLODIndex, this);
    }
  }

 PaintOverlays();
 m_strMultiRenderGuard.unlock();
}

void RenderWindow::ResetRenderingParameters()
{
  FLOATMATRIX4 mIdentity;
  m_mAccumulatedClipTranslation = mIdentity;

  for (int i=0; i < MAX_RENDER_REGIONS; ++i) {
    for (int j=0; j < NUM_WINDOW_MODES; ++j) {
      regionDatas[i][j].clipRotation[0] = mIdentity;
      regionDatas[i][j].clipRotation[1] = mIdentity;

      LuaClassInstance region = luaRenderRegions[i][j];
      SetRotation(region, mIdentity, true);
      SetTranslation(region, mIdentity, true);
      SetClipPlane(region, ExtendedPlane());
    }
  }
  SetWindowFraction2x2(FLOATVECTOR2(0.5f, 0.5f));
  m_Renderer->Transfer3DRotationToMIP();
}


void RenderWindow::SetCurrent1DHistScale(const float value) {
  m_1DHistScale = value;
}

void RenderWindow::SetCurrent2DHistScale(const float value) {
  m_2DHistScale = value;
}

float RenderWindow::GetCurrent1DHistScale() const {
  return m_1DHistScale;
}

float RenderWindow::GetCurrent2DHistScale() const {
  return m_2DHistScale;
}

LuaClassInstance RenderWindow::GetLuaAbstrRenderer() const {
  return m_LuaAbstrRenderer;
}

LuaClassInstance RenderWindow::GetLuaInstance() const {
  return m_LuaThisClass;
}

void RenderWindow::RegisterLuaFunctions(
    LuaClassRegistration<RenderWindow>& reg, RenderWindow* me,
    LuaScripting* ss) {

  ss->vPrint("Registering render window functions.");

  me->m_LuaThisClass = reg.getLuaInstance();

  string id;

  LuaClassInstance ar = me->GetLuaAbstrRenderer();

  // Inherit functions from the Abstract Renderer.
  reg.inherit(ar, "getDataset");
  reg.inherit(ar, "setBGColors");
  reg.inherit(ar, "setTextColor");
  reg.inherit(ar, "setBlendPrecision");
  reg.inherit(ar, "setLogoParams");
  reg.inherit(ar, "setRendererTarget");

  // Register our own functions.
  id = reg.function(&RenderWindow::GetLuaAbstrRenderer, "getRawRenderer",
                    "Returns the Tuvok abstract renderer instance.",
                    false);
  ss->addReturnInfo(id, "Lua class instance of Tuvok's abstract renderer."
      "Generally, you should use the methods exposed by the render window "
      "instead of resorting to raw access to the renderer.");

  id = reg.function(&RenderWindow::SetPerfMeasures, "setPerformanceMeasures",
                    "Sets various performance measures. See info for a detailed"
                    " description of the parameters.", true);
  ss->addParamInfo(id, 0, "minFramerate", "Minimum framerate.");
  ss->addParamInfo(id, 1, "lowResRender", "If true, render low res intermediate"
      "results.");
  ss->addParamInfo(id, 2, "screenResDecFactor", "");  // screen res decrease?
  ss->addParamInfo(id, 3, "sampleDecFactor", "");     // sample rate decrease?
  ss->addParamInfo(id, 4, "LODDelay", "LOD Delay.");
  ss->addParamInfo(id, 5, "activeTS", "");
  ss->addParamInfo(id, 6, "inactiveTS", "");

  id = reg.function(&RenderWindow::CaptureFrame, "screenCapture",
                    "Screenshot of the current volume.", false);
  ss->addParamInfo(id, 0, "filename", "Filename of the screen cap.");
  ss->addParamInfo(id, 1, "preserveTransparency", "True if you want to preserve"
      " transparency in the screen cap.");
}


