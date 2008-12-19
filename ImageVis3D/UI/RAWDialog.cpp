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


//!    File   : RAWDialog.cpp
//!    Author : Jens Krueger
//!             SCI Institute
//!             University of Utah
//!    Date   : December 2008
//
//!    Copyright (C) 2008 SCI Institute

#include "RAWDialog.h"
#include <QtCore/QFileInfo>

using namespace std;

RAWDialog::RAWDialog(string strFilename, UINT64 iFileSize, QWidget* parent /* = 0 */, Qt::WindowFlags flags /* = 0 */) : 
  QDialog(parent, flags),
  m_strFilename(strFilename),
  m_iFileSize(iFileSize)
{
  setupUi(this);
}

RAWDialog::~RAWDialog(void)
{
}

void RAWDialog::setupUi(QDialog *RAWDialog) {
  Ui_RAWDialog::setupUi(RAWDialog);

  /// \todo enable float conversion then enable this button and do also call the convert function accordingly
  radioButton_32BitFloat->setVisible(false);

  QString text = tr("Filename: %1").arg(QFileInfo(m_strFilename.c_str()).fileName());
  label_srcFilename->setText(text);

  CheckValues();
  ToggleEndianessDialog();
}


void RAWDialog::CheckValues() {
  if (!radioButton_RAW->isChecked()) {
    label_Information->setText("Can only validate settings in RAW mode.");
    pushButton_GuessHeader->setEnabled(false);
    return;
  }

  UINT64 iExpectedSize = ComputeExpectedSize();

  if (iExpectedSize < m_iFileSize) {
    label_Information->setText("Settings may work (file is larger then your settings require).");
    pushButton_GuessHeader->setEnabled(true);
  } else if (iExpectedSize > m_iFileSize) {
    label_Information->setText("Settings can not work (file is smaler then your settings require).");
    pushButton_GuessHeader->setEnabled(false);
  } else {
    label_Information->setText("Settings seem to be ok (file has the right size).");
    pushButton_GuessHeader->setEnabled(false);
  }
}

void RAWDialog::ToggleEndianessDialog() {
  groupBox_Endianess->setEnabled(!radioButton_Text->isChecked());
}

void RAWDialog::GuessHeaderSize() {
  UINT64 iExpectedSize = ComputeExpectedSize();

  if ( m_iFileSize >= iExpectedSize) spinBox_HeaderSkip->setValue(m_iFileSize - iExpectedSize);
}

UINT64 RAWDialog::ComputeExpectedSize() {
  UINT64 iCompSize = 0;
  if (radioButton_8bit->isChecked()) iCompSize = 1; else
    if (radioButton_16bit->isChecked()) iCompSize = 2; else
      iCompSize = 4;

  return UINT64(spinBox_SizeX->value()) * UINT64(spinBox_SizeY->value()) * UINT64(spinBox_SizeZ->value()) * iCompSize + UINT64(spinBox_HeaderSkip->value());
}


UINTVECTOR3 RAWDialog::GetSize() {
  return UINTVECTOR3(UINT64(spinBox_SizeX->value()) , UINT64(spinBox_SizeY->value()) , UINT64(spinBox_SizeZ->value()));
}

FLOATVECTOR3 RAWDialog::GetAspectRatio() {
  return FLOATVECTOR3(float(doubleSpinBox_AspX->value()) , float(doubleSpinBox_AspY->value()) , float(doubleSpinBox_AspZ->value()));
}

unsigned int RAWDialog::GetQuantization() {
  unsigned int iID = 0;
  if (radioButton_16bit->isChecked()) iID = 1; else
    if (radioButton_32bitInt->isChecked()) iID = 2; else
      if (radioButton_32BitFloat->isChecked()) iID = 3;
  return iID;
}

unsigned int RAWDialog::GetEncoding() {
  unsigned int iID = 0;
  if (radioButton_Text->isChecked()) iID = 1; else
    if (radioButton_GZIP->isChecked()) iID = 2; else
      if (radioButton_BZIP2->isChecked()) iID = 3;
  return iID;
}

unsigned int RAWDialog::GetHeaderSize() {
  return (unsigned int)(spinBox_HeaderSkip->value());
}

bool RAWDialog::IsBigEndian() {
  return radioButton_BigEnd->isChecked();
}