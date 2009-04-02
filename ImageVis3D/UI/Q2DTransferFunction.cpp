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


//!    File   : Q2DTransferFunction.cpp
//!    Author : Jens Krueger
//!             SCI Institute
//!             University of Utah
//!    Date   : July 2008
//
//!    Copyright (C) 2008 SCI Institute

#include <exception>
#include <limits>
#include "Q2DTransferFunction.h"
#include <QtGui/QPainter>
#include <QtGui/QMouseEvent>
#include "../Tuvok/Controller/MasterController.h"
#include "../Tuvok/Renderer/GPUMemMan/GPUMemMan.h"

#ifdef max
  #undef max
#endif

#ifdef min
  #undef min
#endif

using namespace std;

Q2DTransferFunction::Q2DTransferFunction(MasterController& masterController, QWidget *parent) :
  QTransferFunction(masterController, parent),
  m_pTrans(NULL),
  m_iPaintmode(Q2DT_PAINT_NONE),
  m_iActiveSwatchIndex(-1),
  m_iCachedHeight(0),
  m_iCachedWidth(0),
  m_pBackdropCache(NULL),

  // border size, may be changed arbitrarily
  m_iBorderSize(4),
  m_iSwatchBorderSize(3),

  // mouse motion
  m_iPointSelIndex(-1),
  m_iGradSelIndex(-1),
  m_vMousePressPos(0,0),
  m_bDragging(false),
  m_bDraggingAll(false),
  m_eDragMode(DRM_NONE)
{
  SetColor(isEnabled());
}

Q2DTransferFunction::~Q2DTransferFunction(void)
{
  // delete the backdrop cache pixmap
  delete m_pBackdropCache;
}

QSize Q2DTransferFunction::minimumSizeHint() const
{
  return QSize(50, 50);
}

QSize Q2DTransferFunction::sizeHint() const
{
  return QSize(400, 400);
}

void Q2DTransferFunction::SetData(const Histogram2D* vHistogram, TransferFunction2D* pTrans) {
  m_pTrans = pTrans;
  if (m_pTrans == NULL) return;

  // resize the histogram vector
  m_vHistogram.Resize(vHistogram->GetSize());

  // force the draw routine to recompute the backdrop cache
  m_bBackdropCacheUptodate = false;

  // if the histogram is empty we are done
  if (m_vHistogram.GetSize().area() == 0)  return;

  // rescale the histogram to the [0..1] range
  // first find min and max ...
  unsigned int iMax = vHistogram->GetLinear(0);
  unsigned int iMin = iMax;
  for (size_t i = 0;i<m_vHistogram.GetSize().area();i++) {
    unsigned int iVal = vHistogram->GetLinear(i);
    if (iVal > iMax) iMax = iVal;
    if (iVal < iMin) iMin = iVal;
  }

  // ... than rescale
  float fDiff = float(iMax)-float(iMin);
  for (size_t i = 0;i<m_vHistogram.GetSize().area();i++)
    m_vHistogram.SetLinear(i, (float(vHistogram->GetLinear(i)) - float(iMin)) / fDiff);

  // Upload the new TF to the GPU.
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);

  emit SwatchChange();
}

void Q2DTransferFunction::DrawBorder(QPainter& painter) {
  // draw background with border
  QPen borderPen(m_colorBorder, m_iBorderSize, Qt::SolidLine);
  painter.setPen(borderPen);

  painter.setBrush(m_colorBack);
  QRect backRect(0,0,width(),height());
  painter.drawRect(backRect);
}

void Q2DTransferFunction::DrawHistogram(QPainter& painter) {
  if (m_pTrans == NULL) return;

  // convert the histogram into an image
  // define the bitmap ...
  QImage image(QSize(int(m_vHistogram.GetSize().x), int(m_vHistogram.GetSize().y)), QImage::Format_RGB32);
  for (size_t y = 0;y<m_vHistogram.GetSize().y;y++)
    for (size_t x = 0;x<m_vHistogram.GetSize().x;x++) {
      float value = min<float>(1.0f, pow(m_vHistogram.Get(x,y),1.0f/(1+(m_fHistfScale-1)/100.0f)));
      image.setPixel(int(x),
         int(m_vHistogram.GetSize().y-(y+1)),
         qRgb(int(m_colorBack.red()  * (1.0f-value) +
                  m_colorHistogram.red()  * value),
              int(m_colorBack.green()* (1.0f-value) +
                  m_colorHistogram.green()* value),
              int(m_colorBack.blue() * (1.0f-value) +
                  m_colorHistogram.blue() * value)));
    }

  // ... draw it
  QRectF target(m_iBorderSize/2, m_iBorderSize/2,
    width()-m_iBorderSize, height()-m_iBorderSize);
  QRectF source(0.0, 0.0,
    m_vHistogram.GetSize().x, m_vHistogram.GetSize().y);
  painter.drawImage( target, image, source );
}



INTVECTOR2 Q2DTransferFunction::Rel2Abs(FLOATVECTOR2 vfCoord) {
  return INTVECTOR2(int(m_iSwatchBorderSize/2+
      m_iBorderSize/2+vfCoord.x*
      (width()-m_iBorderSize-m_iSwatchBorderSize)),
        int(m_iSwatchBorderSize/2+
      m_iBorderSize/2+vfCoord.y*
      (height()-m_iBorderSize-m_iSwatchBorderSize)));
}

FLOATVECTOR2 Q2DTransferFunction::Abs2Rel(INTVECTOR2 vCoord) {
  return FLOATVECTOR2((float(vCoord.x)-m_iSwatchBorderSize/2.0f+
           m_iBorderSize/2.0f)/
          float(width()-m_iBorderSize-m_iSwatchBorderSize),
          (float(vCoord.y)-m_iSwatchBorderSize/2.0f+
           m_iBorderSize/2.0f)/
          float(height()-m_iBorderSize-m_iSwatchBorderSize));
}

void Q2DTransferFunction::DrawSwatches(QPainter& painter, bool bDrawWidgets) {
  if (m_pTrans == NULL) return;

  if (bDrawWidgets) {
    painter.setRenderHint(painter.Antialiasing, true);
    painter.translate(+0.5, +0.5);  /// \todo check if we need this
  }

  QPen borderPen(m_colorSwatchBorder,       m_iSwatchBorderSize, Qt::SolidLine);
  QPen noBorderPen(Qt::NoPen);
  QPen circlePen(m_colorSwatchBorderCircle, m_iSwatchBorderSize, Qt::SolidLine);
  QPen gradCircePen(m_colorSwatchGradCircle, m_iSwatchBorderSize/2, Qt::SolidLine);
  QPen circlePenSel(m_colorSwatchBorderCircleSel, m_iSwatchBorderSize, Qt::SolidLine);
  QPen gradCircePenSel(m_colorSwatchGradCircleSel, m_iSwatchBorderSize/2, Qt::SolidLine);

  QBrush solidBrush = QBrush(m_colorSwatchBorderCircle, Qt::SolidPattern);

  // render swatches
  for (size_t i = 0;i<m_pTrans->m_Swatches.size();i++) {
    TFPolygon& currentSwatch = m_pTrans->m_Swatches[i];

    std::vector<QPoint> pointList(currentSwatch.pPoints.size());
    for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {
      INTVECTOR2 vPixelPos = Rel2Abs(currentSwatch.pPoints[j]);
      pointList[j] = QPoint(vPixelPos.x, vPixelPos.y);
    }

    INTVECTOR2 vPixelPos0 = Rel2Abs(currentSwatch.pGradientCoords[0])-INTVECTOR2(m_iSwatchBorderSize, m_iSwatchBorderSize),
		       vPixelPos1 = Rel2Abs(currentSwatch.pGradientCoords[1])-INTVECTOR2(m_iSwatchBorderSize, m_iSwatchBorderSize);
    QLinearGradient linearBrush(vPixelPos0.x, vPixelPos0.y, vPixelPos1.x, vPixelPos1.y);

    for (size_t j = 0;j<currentSwatch.pGradientStops.size();j++) {
      linearBrush.setColorAt(currentSwatch.pGradientStops[j].first,
                   QColor(int(currentSwatch.pGradientStops[j].second[0]*255),
                      int(currentSwatch.pGradientStops[j].second[1]*255),
                          int(currentSwatch.pGradientStops[j].second[2]*255),
                          int(currentSwatch.pGradientStops[j].second[3]*255)));
    }

    if (bDrawWidgets && m_iActiveSwatchIndex == int(i)) painter.setPen(borderPen); else painter.setPen(noBorderPen);
    painter.setBrush(linearBrush);
    painter.drawPolygon(&pointList[0], int(currentSwatch.pPoints.size()));
    painter.setBrush(Qt::NoBrush);

    if (bDrawWidgets && m_iActiveSwatchIndex == int(i)) {
      painter.setBrush(solidBrush);
      for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {
        if (m_iPointSelIndex == int(j)) painter.setPen(circlePenSel); else painter.setPen(circlePen);
        painter.drawEllipse(pointList[j].x()-m_iSwatchBorderSize, pointList[j].y()-m_iSwatchBorderSize, m_iSwatchBorderSize*2, m_iSwatchBorderSize*2);
      }

      painter.setBrush(Qt::NoBrush);
      if (m_iGradSelIndex== 0) painter.setPen(gradCircePenSel); else painter.setPen(gradCircePen);
      INTVECTOR2 vPixelPos = Rel2Abs(currentSwatch.pGradientCoords[0])-INTVECTOR2(m_iSwatchBorderSize,m_iSwatchBorderSize);
      painter.drawEllipse(vPixelPos.x, vPixelPos.y, m_iSwatchBorderSize*2, m_iSwatchBorderSize*2);
      vPixelPos = Rel2Abs(currentSwatch.pGradientCoords[1])-INTVECTOR2(m_iSwatchBorderSize,m_iSwatchBorderSize);
      if (m_iGradSelIndex== 1) painter.setPen(gradCircePenSel); else painter.setPen(gradCircePen);
      painter.drawEllipse(vPixelPos.x, vPixelPos.y, m_iSwatchBorderSize*2, m_iSwatchBorderSize*2);
    }
  }
  if (bDrawWidgets) painter.setRenderHint(painter.Antialiasing, false);
}

void Q2DTransferFunction::mousePressEvent(QMouseEvent *event) {
  if (m_pTrans == NULL) return;
  // call superclass method
  QWidget::mousePressEvent(event);

  bool bShiftPressed = ( event->modifiers() & Qt::ShiftModifier);
  bool bCtrlPressed = ( event->modifiers() & Qt::ControlModifier);

  if (bShiftPressed)
    if(bCtrlPressed)
      m_eDragMode = DRM_ROTATE;
    else
      m_eDragMode = DRM_MOVE;
  else
    if(bCtrlPressed)
      m_eDragMode = DRM_SCALE;
    else
      m_eDragMode = DRM_NONE;


  if (m_iActiveSwatchIndex >= 0 && m_iActiveSwatchIndex<int(m_pTrans->m_Swatches.size())) {
    m_vMousePressPos = INTVECTOR2(event->x(), event->y());
    TFPolygon& currentSwatch = m_pTrans->m_Swatches[m_iActiveSwatchIndex];

    // left mouse drags points around
    if (event->button() == Qt::LeftButton) {

      m_bDragging = true;
      m_bDraggingAll = m_eDragMode != DRM_NONE;

      m_iPointSelIndex = -1;
      m_iGradSelIndex = -1;

      // find closest corner point
      float fMinDist = std::numeric_limits<float>::max();
      for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {
        INTVECTOR2 point = Rel2Abs(currentSwatch.pPoints[j]);

        float fDist = sqrt( float(m_vMousePressPos.x-point.x)*float(m_vMousePressPos.x-point.x) +  float(m_vMousePressPos.y-point.y)*float(m_vMousePressPos.y-point.y) );

        if (fMinDist > fDist) {
          fMinDist = fDist;
          m_iPointSelIndex = int(j);
          m_iGradSelIndex = -1;
        }
      }

      // find closest gradient coord
      for (size_t j = 0;j<2;j++) {
        INTVECTOR2 point = Rel2Abs(currentSwatch.pGradientCoords[j]);

        float fDist = sqrt( float(m_vMousePressPos.x-point.x)*float(m_vMousePressPos.x-point.x) +  float(m_vMousePressPos.y-point.y)*float(m_vMousePressPos.y-point.y) );

        if (fMinDist > fDist) {
          fMinDist = fDist;
          m_iPointSelIndex = -1;
          m_iGradSelIndex = int(j);
        }
      }

    }

    // right mouse removes / adds points
    if (event->button() == Qt::RightButton) {

      FLOATVECTOR2 vfP = Abs2Rel(m_vMousePressPos);

      // find closest edge and compute the point on that edge
      float fMinDist = std::numeric_limits<float>::max();
      FLOATVECTOR2 vfInserCoord;
      int iInsertIndex = -1;

      for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {
        FLOATVECTOR2 A = currentSwatch.pPoints[j];
        FLOATVECTOR2 B = currentSwatch.pPoints[(j+1)%currentSwatch.pPoints.size()];

        // check if we are deleting a point
        if (currentSwatch.pPoints.size() > 3) {
          INTVECTOR2 vPixelDist = Rel2Abs(vfP-A);
          if ( sqrt( float(vPixelDist.x*vPixelDist.x+vPixelDist.y*vPixelDist.y)) <= m_iSwatchBorderSize*3) {
            currentSwatch.pPoints.erase(currentSwatch.pPoints.begin()+j);
            iInsertIndex = -1;
            emit SwatchChange();
            break;
          }
        }


        FLOATVECTOR2 C = vfP - A;    // Vector from a to Point
        float d = (B - A).length();    // Length of the line segment
        FLOATVECTOR2 V = (B - A)/d;    // Unit Vector from A to B
        float t = V^C;          // Intersection point Distance from A

        float fDist;
        if (t >= 0 && t <= d)
          fDist = (vfP-(A + V*t)).length();
        else
          fDist = std::numeric_limits<float>::max();


        if (fDist < fMinDist) {
          fMinDist = fDist;
          vfInserCoord = vfP;
          iInsertIndex = int(j+1);
        }

      }

      if (iInsertIndex >= 0) {
        currentSwatch.pPoints.insert(currentSwatch.pPoints.begin()+iInsertIndex, vfInserCoord);
        emit SwatchChange();
      }
    }
    update();
  }
}

void Q2DTransferFunction::mouseReleaseEvent(QMouseEvent *event) {
  if (m_pTrans == NULL) return;
  // call superclass method
  QWidget::mouseReleaseEvent(event);

  m_bDragging = false;
  m_bDraggingAll = false;
  m_iPointSelIndex = -1;
  m_iGradSelIndex = -1;

  update();

  // send message to update the GLtexture
  if( m_eExecutionMode == ONRELEASE ) ApplyFunction();
}

void Q2DTransferFunction::ApplyFunction() {
  // send message to update the GLtexture
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
}


FLOATVECTOR2 Q2DTransferFunction::Rotate(FLOATVECTOR2 point, float angle, FLOATVECTOR2 center, FLOATVECTOR2 rescale) {
  FLOATVECTOR2 temp, newpoint;
  temp = (point - center)*rescale;
  newpoint.x= temp.x*cos(angle)-temp.y*sin(angle);
  newpoint.y= temp.x*sin(angle)+temp.y*cos(angle);
  return (newpoint/rescale)+center;
}

void Q2DTransferFunction::mouseMoveEvent(QMouseEvent *event) {
  if (m_pTrans == NULL) return;
  // call superclass method
  QWidget::mouseMoveEvent(event);

  if (m_bDragging) {

    INTVECTOR2 vMouseCurrentPos(event->x(), event->y());

    FLOATVECTOR2 vfPressPos = Abs2Rel(m_vMousePressPos);
    FLOATVECTOR2 vfCurrentPos = Abs2Rel(vMouseCurrentPos);

    FLOATVECTOR2 vfDelta = vfCurrentPos-vfPressPos;

    TFPolygon& currentSwatch = m_pTrans->m_Swatches[m_iActiveSwatchIndex];

    if (m_bDraggingAll)  {
      switch (m_eDragMode) {
        case DRM_MOVE : {
                  for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++) currentSwatch.pPoints[i] += vfDelta;
                  currentSwatch.pGradientCoords[0] += vfDelta;
                  currentSwatch.pGradientCoords[1] += vfDelta;
                } break;
        case DRM_ROTATE : {
                  float fScaleFactor = vfDelta.x + vfDelta.y;
                  FLOATVECTOR2 vfCenter(0,0);
                  for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++) vfCenter += currentSwatch.pPoints[i];

                  vfCenter /= currentSwatch.pPoints.size();
                  FLOATVECTOR2 fvRot(cos(fScaleFactor/10),sin(fScaleFactor/10));

                  FLOATVECTOR2 vfRescale(width(),height());
                  vfRescale /= max(width(),height());

                  for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++) currentSwatch.pPoints[i] = Rotate(currentSwatch.pPoints[i], fScaleFactor, vfCenter, vfRescale);
                  currentSwatch.pGradientCoords[0] = Rotate(currentSwatch.pGradientCoords[0], fScaleFactor, vfCenter, vfRescale);
                  currentSwatch.pGradientCoords[1] = Rotate(currentSwatch.pGradientCoords[1], fScaleFactor, vfCenter, vfRescale);


                  } break;
        case DRM_SCALE : {
                  float fScaleFactor = vfDelta.x + vfDelta.y;
                  FLOATVECTOR2 vfCenter(0,0);
                  for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++) vfCenter += currentSwatch.pPoints[i];

                  vfCenter /= currentSwatch.pPoints.size();

                  for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++)
                    currentSwatch.pPoints[i] += (currentSwatch.pPoints[i]-vfCenter)*fScaleFactor;
                  currentSwatch.pGradientCoords[0] += (currentSwatch.pGradientCoords[0]-vfCenter)*fScaleFactor;
                  currentSwatch.pGradientCoords[1] += (currentSwatch.pGradientCoords[1]-vfCenter)*fScaleFactor;

                  } break;
        default : break;
      }
    } else {
      if (m_iPointSelIndex >= 0)  {
        currentSwatch.pPoints[m_iPointSelIndex] += vfDelta;
      } else {
        currentSwatch.pGradientCoords[m_iGradSelIndex] += vfDelta;
      }
    }

    m_vMousePressPos = vMouseCurrentPos;

    update();

    // send message to update the GLtexture
    if( m_eExecutionMode == CONTINUOUS ) ApplyFunction();
  }
}

void Q2DTransferFunction::SetColor(bool bIsEnabled) {
    if (bIsEnabled) {
      m_colorHistogram = QColor(255,255,255);
      m_colorBack = QColor(Qt::black);
      m_colorBorder = QColor(255, 255, 255);
      m_colorSwatchBorder = QColor(180, 0, 0);
      m_colorSwatchBorderCircle = QColor(200, 200, 0);
      m_colorSwatchGradCircle = QColor(0, 255, 0);
      m_colorSwatchGradCircleSel = QColor(255, 255, 255);
      m_colorSwatchBorderCircleSel = QColor(255, 255, 255);
    } else {
      m_colorHistogram = QColor(55,55,55);
      m_colorBack = QColor(Qt::black);
      m_colorBorder = QColor(100, 100, 100);
      m_colorSwatchBorder = QColor(100, 50, 50);
      m_colorSwatchBorderCircle = QColor(100, 100, 50);
      m_colorSwatchGradCircle = QColor(50, 100, 50);
      m_colorSwatchGradCircleSel = m_colorSwatchGradCircle;
      m_colorSwatchBorderCircleSel = m_colorSwatchBorderCircle;
    }
}

void Q2DTransferFunction::changeEvent(QEvent * event) {
  // call superclass method
  QWidget::changeEvent(event);

  if (event->type() == QEvent::EnabledChange) {
    SetColor(isEnabled());
    m_bBackdropCacheUptodate = false;
    update();
  }
}


void Q2DTransferFunction::Draw1DTrans(QPainter& painter) {
  QRect imageRect(m_iBorderSize/2, m_iBorderSize/2, width()-m_iBorderSize, height()-m_iBorderSize);
  painter.drawImage(imageRect,m_pTrans->Get1DTransImage());
}

void Q2DTransferFunction::paintEvent(QPaintEvent *event) {
  // call superclass method
  QWidget::paintEvent(event);

  if (m_pTrans == NULL) {
    QPainter painter(this);
    DrawBorder(painter);
    return;
  }

  // as drawing the histogram can become quite expensive we'll cache it in an image and only redraw if needed
  if (!m_bBackdropCacheUptodate || (unsigned int)height() != m_iCachedHeight || (unsigned int)width() != m_iCachedWidth) {

    // delete the old pixmap an create a new one if the size has changed
    if ((unsigned int)height() != m_iCachedHeight || (unsigned int)width() != m_iCachedWidth) {
      delete m_pBackdropCache;
      m_pBackdropCache = new QPixmap(width(),height());
    }

    // attach a painter to the pixmap
    QPainter image_painter(m_pBackdropCache);

    // draw the backdrop into the image
    DrawBorder(image_painter);
    DrawHistogram(image_painter);
    Draw1DTrans(image_painter);

    // update change detection states
    m_bBackdropCacheUptodate = true;
    m_iCachedHeight = height();
    m_iCachedWidth = width();
  }

  // now draw everything rest into this widget
  QPainter painter(this);

  // the image captured before (or cached from a previous call)
  painter.drawImage(0,0,m_pBackdropCache->toImage());

  // and the swatches
  DrawSwatches(painter, true);
}

bool Q2DTransferFunction::LoadFromFile(const QString& strFilename) {
  // hand the load call over to the TransferFunction1D class
  if( m_pTrans->Load(strFilename.toStdString(), m_pTrans->GetSize() ) ) {
    m_iActiveSwatchIndex = 0;
    m_bBackdropCacheUptodate = false;
    update();
    m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
    emit SwatchChange();
    return true;
  } else return false;
}

bool Q2DTransferFunction::SaveToFile(const QString& strFilename) {
  // hand the save call over to the TransferFunction1D class
  return m_pTrans->Save(strFilename.toStdString());
}


void Q2DTransferFunction::Set1DTrans(const TransferFunction1D* p1DTrans) {
  m_pTrans->Update1DTrans(p1DTrans);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  m_bBackdropCacheUptodate = false;
  update();
}

void Q2DTransferFunction::Transfer2DSetActiveSwatch(const int iActiveSwatch) {
  if (iActiveSwatch == -1 && m_pTrans->m_Swatches.size() > 0) return;
  m_iActiveSwatchIndex = iActiveSwatch;
  update();
}

void Q2DTransferFunction::Transfer2DAddCircleSwatch() {
  TFPolygon newSwatch;

  FLOATVECTOR2 vPoint(0.8f,0.8f);
  unsigned int iNumberOfSegments = 20;
  for (unsigned int i = 0;i<iNumberOfSegments;i++) {
    newSwatch.pPoints.push_back(vPoint);
    vPoint = Rotate(vPoint, 6.283185f/float(iNumberOfSegments), FLOATVECTOR2(0.5f,0.5f), FLOATVECTOR2(1,1));
  }

  newSwatch.pGradientCoords[0] = FLOATVECTOR2(0,0.5f);
  newSwatch.pGradientCoords[1] = FLOATVECTOR2(1,0.5f);

  GradientStop g1(0,FLOATVECTOR4(0,0,0,0)),g2(0.5f,FLOATVECTOR4(1,1,1,1)),g3(1,FLOATVECTOR4(0,0,0,0));
  newSwatch.pGradientStops.push_back(g1);
  newSwatch.pGradientStops.push_back(g2);
  newSwatch.pGradientStops.push_back(g3);

  m_pTrans->m_Swatches.push_back(newSwatch);

  m_iActiveSwatchIndex = int(m_pTrans->m_Swatches.size()-1);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  emit SwatchChange();
}

void Q2DTransferFunction::Transfer2DAddSwatch() {
  TFPolygon newSwatch;

  newSwatch.pPoints.push_back(FLOATVECTOR2(0.3f,0.3f));
  newSwatch.pPoints.push_back(FLOATVECTOR2(0.3f,0.7f));
  newSwatch.pPoints.push_back(FLOATVECTOR2(0.7f,0.7f));
  newSwatch.pPoints.push_back(FLOATVECTOR2(0.7f,0.3f));

  newSwatch.pGradientCoords[0] = FLOATVECTOR2(0.3f,0.5f);
  newSwatch.pGradientCoords[1] = FLOATVECTOR2(0.7f,0.5f);

  GradientStop g1(0,FLOATVECTOR4(0,0,0,0)),g2(0.5f,FLOATVECTOR4(1,1,1,1)),g3(1,FLOATVECTOR4(0,0,0,0));
  newSwatch.pGradientStops.push_back(g1);
  newSwatch.pGradientStops.push_back(g2);
  newSwatch.pGradientStops.push_back(g3);

  m_pTrans->m_Swatches.push_back(newSwatch);

  m_iActiveSwatchIndex = int(m_pTrans->m_Swatches.size()-1);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  emit SwatchChange();
}

void Q2DTransferFunction::Transfer2DDeleteSwatch(){
  if (m_iActiveSwatchIndex != -1) {
    m_pTrans->m_Swatches.erase(m_pTrans->m_Swatches.begin()+m_iActiveSwatchIndex);

    m_iActiveSwatchIndex = min<int>(m_iActiveSwatchIndex, int(m_pTrans->m_Swatches.size()-1));
    m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
    emit SwatchChange();
  }
}

void Q2DTransferFunction::Transfer2DUpSwatch(){
  if (m_iActiveSwatchIndex > 0) {
    TFPolygon tmp = m_pTrans->m_Swatches[m_iActiveSwatchIndex-1];
    m_pTrans->m_Swatches[m_iActiveSwatchIndex-1] = m_pTrans->m_Swatches[m_iActiveSwatchIndex];
    m_pTrans->m_Swatches[m_iActiveSwatchIndex] = tmp;

    m_iActiveSwatchIndex--;
    m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
    emit SwatchChange();
  }
}

void Q2DTransferFunction::Transfer2DDownSwatch(){
  if (m_iActiveSwatchIndex >= 0 && m_iActiveSwatchIndex < int(m_pTrans->m_Swatches.size()-1)) {
    TFPolygon tmp = m_pTrans->m_Swatches[m_iActiveSwatchIndex+1];
    m_pTrans->m_Swatches[m_iActiveSwatchIndex+1] = m_pTrans->m_Swatches[m_iActiveSwatchIndex];
    m_pTrans->m_Swatches[m_iActiveSwatchIndex] = tmp;

    m_iActiveSwatchIndex++;
    m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
    emit SwatchChange();
  }
}


void Q2DTransferFunction::AddGradient(GradientStop stop) {
  for (std::vector< GradientStop >::iterator i = m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.begin();i<m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.end();i++) {
    if (i->first > stop.first) {
      m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.insert(i, stop);
      return;
    }
  }
  m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.push_back(stop);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  update();
}

void Q2DTransferFunction::DeleteGradient(unsigned int i) {
  m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.erase(m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.begin()+i);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  update();
}

void Q2DTransferFunction::SetGradient(unsigned int i, GradientStop stop) {
  m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops[i] = stop;
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  update();
}
