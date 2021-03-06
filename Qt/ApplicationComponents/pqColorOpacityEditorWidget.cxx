/*=========================================================================

   Program: ParaView
   Module:    $RCSfile$

   Copyright (c) 2005,2006 Sandia Corporation, Kitware Inc.
   All rights reserved.

   ParaView is a free software; you can redistribute it and/or modify it
   under the terms of the ParaView license version 1.2.

   See License_v1.2.txt for the full ParaView license.
   A copy of this license can be obtained by contacting
   Kitware Inc.
   28 Corporate Drive
   Clifton Park, NY 12065
   USA

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

========================================================================*/
#include "pqColorOpacityEditorWidget.h"
#include "ui_pqColorOpacityEditorWidget.h"
#include "ui_pqSavePresetOptions.h"

#include "pqActiveObjects.h"
#include "pqChooseColorPresetReaction.h"
#include "pqColorTableModel.h"
#include "pqDataRepresentation.h"
#include "pqOpacityTableModel.h"
#include "pqPipelineRepresentation.h"
#include "pqPropertiesPanel.h"
#include "pqPropertyWidgetDecorator.h"
#include "pqRescaleRange.h"
#include "pqResetScalarRangeReaction.h"
#include "pqSettings.h"
#include "pqTransferFunctionWidget.h"
#include "pqUndoStack.h"
#include "vtkCommand.h"
#include "vtkDiscretizableColorTransferFunction.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkNew.h"
#include "vtkPiecewiseFunction.h"
#include "vtkPVXMLElement.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMPVRepresentationProxy.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMTransferFunctionPresets.h"
#include "vtkSMTransferFunctionProxy.h"
#include "vtkVector.h"
#include "vtkWeakPointer.h"
#include "vtk_jsoncpp.h"

#include <QDoubleValidator>
#include <QMessageBox>
#include <QPointer>
#include <QtDebug>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
//-----------------------------------------------------------------------------
// Decorator used to hide the widget when using IndexedLookup.
class pqColorOpacityEditorWidgetDecorator : public pqPropertyWidgetDecorator
{
  typedef pqPropertyWidgetDecorator Superclass;
  bool Hidden;
public:
  pqColorOpacityEditorWidgetDecorator(vtkPVXMLElement* xmlArg, pqPropertyWidget* parentArg)
    : Superclass(xmlArg, parentArg), Hidden(false)
    {
    }
  virtual ~pqColorOpacityEditorWidgetDecorator()
    {
    }

  void setHidden(bool val)
    {
    if (val != this->Hidden) { this->Hidden = val; emit this->visibilityChanged(); }
    }
  virtual bool canShowWidget(bool show_advanced) const
    {
    Q_UNUSED(show_advanced);
    return !this->Hidden;
    }

private:
  Q_DISABLE_COPY(pqColorOpacityEditorWidgetDecorator)
};

} // end anonymous namespace

//-----------------------------------------------------------------------------
class pqColorOpacityEditorWidget::pqInternals
{
public:
  Ui::ColorOpacityEditorWidget Ui;
  pqColorTableModel ColorTableModel;
  pqOpacityTableModel OpacityTableModel;
  QPointer<pqColorOpacityEditorWidgetDecorator> Decorator;
  vtkWeakPointer<vtkSMPropertyGroup> PropertyGroup;
  vtkWeakPointer<vtkSMProxy> ScalarOpacityFunctionProxy;

  // We use this pqPropertyLinks instance to simply monitor smproperty changes.
  pqPropertyLinks LinksForMonitoringChanges;
  vtkNew<vtkEventQtSlotConnect> VTKConnector;

  pqInternals(pqColorOpacityEditorWidget* self, vtkSMPropertyGroup* group)
    : ColorTableModel(self), OpacityTableModel(self), PropertyGroup(group)
    {
    this->Ui.setupUi(self);
    this->Ui.CurrentDataValue->setValidator(new QDoubleValidator(self));
    this->Ui.mainLayout->setMargin(pqPropertiesPanel::suggestedMargin());
    //this->Ui.mainLayout->setSpacing(
    //  pqPropertiesPanel::suggestedVerticalSpacing());

    this->Decorator = new pqColorOpacityEditorWidgetDecorator(NULL, self);

    this->Ui.ColorTable->setModel(&this->ColorTableModel);
    this->Ui.ColorTable->horizontalHeader()->setHighlightSections(false);
#if QT_VERSION >= 0x050000
    this->Ui.ColorTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
#else
    this->Ui.ColorTable->horizontalHeader()->setResizeMode(QHeaderView::Stretch);
#endif
    this->Ui.ColorTable->horizontalHeader()->setStretchLastSection(true);

    this->Ui.OpacityTable->setModel(&this->OpacityTableModel);
    this->Ui.OpacityTable->horizontalHeader()->setHighlightSections(false);
#if QT_VERSION >= 0x050000
    this->Ui.OpacityTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
#else
    this->Ui.OpacityTable->horizontalHeader()->setResizeMode(QHeaderView::Stretch);
#endif
    this->Ui.OpacityTable->horizontalHeader()->setStretchLastSection(true);
    }

  void render()
  {
    pqDataRepresentation* repr =
      pqActiveObjects::instance().activeRepresentation();
    if (repr)
      {
      repr->renderViewEventually();
      return;
      }
    pqView* activeView = pqActiveObjects::instance().activeView();
    if (activeView)
      {
      activeView->render();
      return;
      }
    pqApplicationCore::instance()->render();
  }
};

//-----------------------------------------------------------------------------
pqColorOpacityEditorWidget::pqColorOpacityEditorWidget(
  vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parentObject)
  : Superclass(smproxy, parentObject),
  Internals(new pqInternals(this, smgroup))
{
  Ui::ColorOpacityEditorWidget &ui = this->Internals->Ui;
  vtkDiscretizableColorTransferFunction* stc =
    vtkDiscretizableColorTransferFunction::SafeDownCast(
      this->proxy()->GetClientSideObject());
  if (stc)
    {
    ui.ColorEditor->initialize(stc, true, NULL, false);
    QObject::connect(
      &this->Internals->ColorTableModel,
      SIGNAL(dataChanged(const QModelIndex &, const QModelIndex &)),
      this, SIGNAL(xrgbPointsChanged()));
    }
  QObject::connect(&pqActiveObjects::instance(),
    SIGNAL(representationChanged(pqRepresentation*)),
    this, SLOT(updateButtonEnableState()));
  QObject::connect(&pqActiveObjects::instance(),
    SIGNAL(viewChanged(pqView*)),
    this, SLOT(updateButtonEnableState()));

  QObject::connect(
    ui.OpacityEditor, SIGNAL(currentPointChanged(vtkIdType)),
    this, SLOT(opacityCurrentChanged(vtkIdType)));
  QObject::connect(
    ui.ColorEditor, SIGNAL(currentPointChanged(vtkIdType)),
    this, SLOT(colorCurrentChanged(vtkIdType)));

  QObject::connect(
    ui.ColorEditor, SIGNAL(controlPointsModified()),
    this, SIGNAL(xrgbPointsChanged()));
  QObject::connect(
    ui.OpacityEditor, SIGNAL(controlPointsModified()),
    this, SIGNAL(xvmsPointsChanged()));

  QObject::connect(
    ui.ColorEditor, SIGNAL(controlPointsModified()),
    this, SLOT(updateCurrentData()));
  QObject::connect(
    ui.OpacityEditor, SIGNAL(controlPointsModified()),
    this, SLOT(updateCurrentData()));

  QObject::connect(
    ui.ResetRangeToData, SIGNAL(clicked()),
    this, SLOT(resetRangeToData()));

  QObject::connect(
    ui.ResetRangeToCustom, SIGNAL(clicked()),
    this, SLOT(resetRangeToCustom()));

  QObject::connect(
    ui.ResetRangeToDataOverTime, SIGNAL(clicked()),
    this, SLOT(resetRangeToDataOverTime()));

  QObject::connect(
    ui.ResetRangeToVisibleData, SIGNAL(clicked()),
    this, SLOT(resetRangeToVisibleData()));

  QObject::connect(
    ui.InvertTransferFunctions, SIGNAL(clicked()),
    this, SLOT(invertTransferFunctions()));

  QObject::connect(ui.ChoosePreset, SIGNAL(clicked()),
    this, SLOT(choosePreset()));
  QObject::connect(ui.SaveAsPreset, SIGNAL(clicked()),
    this, SLOT(saveAsPreset()));
  QObject::connect(ui.AdvancedButton, SIGNAL(clicked()),
    this, SLOT(updatePanel()));

  // if the user edits the "DataValue", we need to update the transfer function.
  QObject::connect(ui.CurrentDataValue, SIGNAL(textChangedAndEditingFinished()),
    this, SLOT(currentDataEdited()));

  vtkSMProperty* smproperty = smgroup->GetProperty("XRGBPoints");
  if (smproperty)
    {
    this->addPropertyLink(
      this, "xrgbPoints", SIGNAL(xrgbPointsChanged()), smproperty);
    }
  else
    {
    qCritical("Missing 'XRGBPoints' property. Widget may not function correctly.");
    }

  smproperty = smproxy->GetProperty("LockScalarRange");
  if (smproperty)
    {
    this->addPropertyLink(
      this, "lockScalarRange", SIGNAL(lockScalarRangeChanged()), smproperty);
    }

  ui.OpacityEditor->hide();
  smproperty = smgroup->GetProperty("ScalarOpacityFunction");
  if (smproperty)
    {
    this->addPropertyLink(
      this, "scalarOpacityFunctionProxy", SIGNAL(scalarOpacityFunctionProxyChanged()),
      smproperty);
    }

  smproperty = smgroup->GetProperty("EnableOpacityMapping");
  if (smproperty)
    {
    this->addPropertyLink(
      ui.EnableOpacityMapping, "checked", SIGNAL(toggled(bool)), smproperty);
    }
  else
    {
    ui.EnableOpacityMapping->hide();
    }

  smproperty = smgroup->GetProperty("UseLogScale");
  if (smproperty)
    {
    this->addPropertyLink(
      this, "useLogScale", SIGNAL(useLogScaleChanged()), smproperty);
    QObject::connect(ui.UseLogScale, SIGNAL(clicked(bool)),
      this, SLOT(useLogScaleClicked(bool)));
    // QObject::connect(ui.UseLogScale, SIGNAL(toggled(bool)),
    //  this, SIGNAL(useLogScaleChanged()));
    }
  else
    {
    ui.UseLogScale->hide();
    }

  // if proxy has a property named IndexedLookup, we hide this entire widget
  // when IndexedLookup is ON.
  if (smproxy->GetProperty("IndexedLookup"))
    {
    // we are not controlling the IndexedLookup property, we are merely
    // observing it to ensure the UI is updated correctly. Hence we don't fire
    // any signal to update the smproperty.
    this->Internals->VTKConnector->Connect(
      smproxy->GetProperty("IndexedLookup"), vtkCommand::ModifiedEvent,
      this, SLOT(updateIndexedLookupState()));
    this->updateIndexedLookupState();

    // Add decorator so the widget can be hidden when IndexedLookup is ON.
    this->addDecorator(this->Internals->Decorator);
    }

  pqSettings *settings = pqApplicationCore::instance()->settings();
  if (settings)
    {
    this->Internals->Ui.AdvancedButton->setChecked(
      settings->value("showAdvancedPropertiesColorOpacityEditorWidget", false).toBool());
    }

  this->updateCurrentData();
  this->updatePanel();
}

//-----------------------------------------------------------------------------
pqColorOpacityEditorWidget::~pqColorOpacityEditorWidget()
{
  pqSettings *settings = pqApplicationCore::instance()->settings();
  if (settings)
    {
    // save the state of the advanced button in the widget
    settings->setValue("showAdvancedPropertiesColorOpacityEditorWidget",
                       this->Internals->Ui.AdvancedButton->isChecked());
    }

  delete this->Internals;
  this->Internals = NULL;
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::setScalarOpacityFunctionProxy(pqSMProxy sofProxy)
{
  pqInternals& internals = (*this->Internals);
  Ui::ColorOpacityEditorWidget& ui = internals.Ui;

  vtkSMProxy* newSofProxy = NULL;
  vtkPiecewiseFunction* pwf = sofProxy?
    vtkPiecewiseFunction::SafeDownCast(sofProxy->GetClientSideObject()) : NULL;
  if (sofProxy && sofProxy->GetProperty("Points") && pwf)
    {
    newSofProxy = sofProxy.GetPointer();
    }
  if (internals.ScalarOpacityFunctionProxy == newSofProxy)
    {
    return;
    }
  if (internals.ScalarOpacityFunctionProxy)
    {
    // cleanup old property links.
    this->links().removePropertyLink(
      this, "xvmsPoints", SIGNAL(xvmsPointsChanged()),
      internals.ScalarOpacityFunctionProxy,
      internals.ScalarOpacityFunctionProxy->GetProperty("Points"));
    }
  internals.ScalarOpacityFunctionProxy = newSofProxy;
  if (internals.ScalarOpacityFunctionProxy)
    {
    // FIXME: need to verify that repeated initializations are okay.
    ui.OpacityEditor->initialize(
      vtkScalarsToColors::SafeDownCast(this->proxy()->GetClientSideObject()), false, pwf, true);
    // add new property links.
    this->links().addPropertyLink(
      this, "xvmsPoints", SIGNAL(xvmsPointsChanged()),
      internals.ScalarOpacityFunctionProxy,
      internals.ScalarOpacityFunctionProxy->GetProperty("Points"));
    }
  ui.OpacityEditor->setVisible(newSofProxy != NULL);
}

//-----------------------------------------------------------------------------
pqSMProxy pqColorOpacityEditorWidget::scalarOpacityFunctionProxy() const
{
  return this->Internals->ScalarOpacityFunctionProxy.GetPointer();
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::updateIndexedLookupState()
{
  if (this->proxy()->GetProperty("IndexedLookup"))
    {
    bool val = vtkSMPropertyHelper(this->proxy(), "IndexedLookup").GetAsInt() != 0;
    this->Internals->Decorator->setHidden(val);
    }
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::opacityCurrentChanged(vtkIdType index)
{
  if (index != -1)
    {
    Ui::ColorOpacityEditorWidget &ui = this->Internals->Ui;
    ui.ColorEditor->setCurrentPoint(-1);
    }
  this->updateCurrentData();
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::colorCurrentChanged(vtkIdType index)
{
  if (index != -1)
    {
    Ui::ColorOpacityEditorWidget &ui = this->Internals->Ui;
    ui.OpacityEditor->setCurrentPoint(-1);
    }
  this->updateCurrentData();
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::updatePanel()
{
  if (this->Internals)
    {
    bool advancedVisible = this->Internals->Ui.AdvancedButton->isChecked();
    this->Internals->Ui.ColorLabel->setVisible(advancedVisible);
    this->Internals->Ui.ColorTable->setVisible(advancedVisible);
    this->Internals->Ui.OpacityLabel->setVisible(advancedVisible);
    this->Internals->Ui.OpacityTable->setVisible(advancedVisible);
    }
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::updateCurrentData()
{
  vtkDiscretizableColorTransferFunction* stc =
    vtkDiscretizableColorTransferFunction::SafeDownCast(
      this->proxy()->GetClientSideObject());
  vtkSMProxy* pwfProxy = this->scalarOpacityFunctionProxy();
  vtkPiecewiseFunction* pwf = pwfProxy?
    vtkPiecewiseFunction::SafeDownCast(pwfProxy->GetClientSideObject()) : NULL;

  Ui::ColorOpacityEditorWidget &ui = this->Internals->Ui;
  if (ui.ColorEditor->currentPoint() >= 0 && stc)
    {
    double xrgbms[6];
    stc->GetNodeValue(ui.ColorEditor->currentPoint(), xrgbms);
    ui.CurrentDataValue->setText(QString::number(xrgbms[0]));

    // Don't enable widget for first/last control point. For those, users must
    // rescale the transfer function manually
    ui.CurrentDataValue->setEnabled(
      ui.ColorEditor->currentPoint() != 0 &&
      ui.ColorEditor->currentPoint() !=
      (ui.ColorEditor->numberOfControlPoints()-1));
    }
  else if (ui.OpacityEditor->currentPoint() >= 0 && pwf)
    {
    double xvms[4];
    pwf->GetNodeValue(ui.OpacityEditor->currentPoint(), xvms);
    ui.CurrentDataValue->setText(QString::number(xvms[0]));

    // Don't enable widget for first/last control point. For those, users must
    // rescale the transfer function manually
    ui.CurrentDataValue->setEnabled(
      ui.OpacityEditor->currentPoint() != 0 &&
      ui.OpacityEditor->currentPoint() !=
      (ui.OpacityEditor->numberOfControlPoints()-1));
    }
  else
    {
    ui.CurrentDataValue->setEnabled(false);
    }

  this->Internals->ColorTableModel.refresh();
  this->Internals->OpacityTableModel.refresh();
}

//-----------------------------------------------------------------------------
QList<QVariant> pqColorOpacityEditorWidget::xrgbPoints() const
{
  vtkDiscretizableColorTransferFunction* stc =
    vtkDiscretizableColorTransferFunction::SafeDownCast(
      this->proxy()->GetClientSideObject());
  QList<QVariant> values;
  for (int cc=0; stc != NULL && cc < stc->GetSize(); cc++)
    {
    double xrgbms[6];
    stc->GetNodeValue(cc, xrgbms);
    vtkVector<double, 4> value;
    values.push_back(xrgbms[0]);
    values.push_back(xrgbms[1]);
    values.push_back(xrgbms[2]);
    values.push_back(xrgbms[3]);
    }

  return values;
}

//-----------------------------------------------------------------------------
QList<QVariant> pqColorOpacityEditorWidget::xvmsPoints() const
{
  vtkSMProxy* pwfProxy = this->scalarOpacityFunctionProxy();
  vtkPiecewiseFunction* pwf = pwfProxy?
    vtkPiecewiseFunction::SafeDownCast(pwfProxy->GetClientSideObject()) : NULL;

  QList<QVariant> values;
  for (int cc=0; pwf != NULL && cc < pwf->GetSize(); cc++)
    {
    double xvms[4];
    pwf->GetNodeValue(cc, xvms);
    values.push_back(xvms[0]);
    values.push_back(xvms[1]);
    values.push_back(xvms[2]);
    values.push_back(xvms[3]);
    }
  return values;
}

//-----------------------------------------------------------------------------
bool pqColorOpacityEditorWidget::useLogScale() const
{
  return this->Internals->Ui.UseLogScale->isChecked();
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::setUseLogScale(bool val)
{
  Ui::ColorOpacityEditorWidget &ui = this->Internals->Ui;
  ui.UseLogScale->setChecked(val);
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::useLogScaleClicked(bool log_space)
{
  if (log_space)
    {
    vtkSMTransferFunctionProxy::MapControlPointsToLogSpace(this->proxy());
    }
  else
    {
    vtkSMTransferFunctionProxy::MapControlPointsToLinearSpace(this->proxy());
    }

  // FIXME: ensure scalar range is valid.

  emit this->useLogScaleChanged();
}

//-----------------------------------------------------------------------------
bool pqColorOpacityEditorWidget::lockScalarRange() const
{
  return vtkSMPropertyHelper(this->proxy(), "LockScalarRange").GetAsInt() ? true : false;
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::setLockScalarRange(bool val)
{
  vtkSMPropertyHelper(this->proxy(), "LockScalarRange").Set(val ? 1 : 0);
  this->proxy()->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::setXvmsPoints(const QList<QVariant>& values)
{
  Q_UNUSED(values);
  // Since the vtkPiecewiseFunction connected to the widget is directly obtained
  // from the proxy, we don't need to do anything here. The widget will be
  // updated when the proxy updates.
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::setXrgbPoints(const QList<QVariant>& values)
{
  Q_UNUSED(values);
  // Since the vtkColorTransferFunction connected to the widget is directly obtained
  // from the proxy, we don't need to do anything here. The widget will be
  // updated when the proxy updates.
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::currentDataEdited()
{
  vtkDiscretizableColorTransferFunction* stc =
    vtkDiscretizableColorTransferFunction::SafeDownCast(
      this->proxy()->GetClientSideObject());
  vtkSMProxy* pwfProxy = this->scalarOpacityFunctionProxy();
  vtkPiecewiseFunction* pwf = pwfProxy?
    vtkPiecewiseFunction::SafeDownCast(pwfProxy->GetClientSideObject()) : NULL;

  Ui::ColorOpacityEditorWidget &ui = this->Internals->Ui;
  if (ui.ColorEditor->currentPoint() >= 0 && stc)
    {
    ui.ColorEditor->setCurrentPointPosition(
      ui.CurrentDataValue->text().toDouble());
    }
  else if (ui.OpacityEditor->currentPoint() >= 0 && pwf)
    {
    ui.OpacityEditor->setCurrentPointPosition(
      ui.CurrentDataValue->text().toDouble());
    }

  this->updateCurrentData();
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::updateButtonEnableState()
{
  pqDataRepresentation* repr =
    pqActiveObjects::instance().activeRepresentation();
  bool hasRepresentation = repr != NULL;
  pqView* activeView = pqActiveObjects::instance().activeView();
  bool hasView = activeView != NULL;

  Ui::ColorOpacityEditorWidget &ui = this->Internals->Ui;
  ui.ResetRangeToData->setEnabled(hasRepresentation);
  ui.ResetRangeToDataOverTime->setEnabled(hasRepresentation);
  ui.ResetRangeToVisibleData->setEnabled(hasRepresentation && hasView);
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::resetRangeToData()
{
  // passing in NULL ensure pqResetScalarRangeReaction simply uses active representation.
  if (pqResetScalarRangeReaction::resetScalarRangeToData(NULL))
    {
    this->Internals->render();
    emit this->changeFinished();
    }
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::resetRangeToDataOverTime()
{
  // passing in NULL ensure pqResetScalarRangeReaction simply uses active representation.
  if (pqResetScalarRangeReaction::resetScalarRangeToDataOverTime(NULL))
    {
    this->Internals->render();
    emit this->changeFinished();
    }
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::resetRangeToVisibleData()
{
  pqPipelineRepresentation* repr = qobject_cast<pqPipelineRepresentation*>(
    pqActiveObjects::instance().activeRepresentation());
  if (!repr)
    {
    qCritical() << "No active representation.";
    return;
    }

  vtkSMPVRepresentationProxy* repProxy =
    vtkSMPVRepresentationProxy::SafeDownCast(repr->getProxy());
  if (!repProxy)
    {
    return ;
    }

  pqView* activeView = pqActiveObjects::instance().activeView();
  if (!activeView)
    {
    qCritical() << "No active view.";
    return;
    }

  vtkSMRenderViewProxy* rvproxy = vtkSMRenderViewProxy::SafeDownCast(activeView->getViewProxy());
  if (!rvproxy)
    {
    return;
    }

  BEGIN_UNDO_SET("Reset transfer function ranges using visible data");
  vtkSMPVRepresentationProxy::RescaleTransferFunctionToVisibleRange(repProxy, rvproxy);
  this->Internals->render();
  END_UNDO_SET();
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::resetRangeToCustom()
{
  vtkDiscretizableColorTransferFunction* stc =
    vtkDiscretizableColorTransferFunction::SafeDownCast(
      this->proxy()->GetClientSideObject());
  double range[2];
  stc->GetRange(range);

  pqRescaleRange dialog(this);
  dialog.setRange(range[0], range[1]);
  if (dialog.exec() == QDialog::Accepted)
    {
    this->resetRangeToCustom(dialog.getMinimum(), dialog.getMaximum());
    }
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::resetRangeToCustom(double min, double max)
{
  BEGIN_UNDO_SET("Reset transfer function ranges");
  vtkSMTransferFunctionProxy::RescaleTransferFunction(this->proxy(), min, max);
  if (vtkSMProxy* sofProxy = this->scalarOpacityFunctionProxy())
    {
    vtkSMTransferFunctionProxy::RescaleTransferFunction(sofProxy, min, max);
    }
  // disable auto-rescale of transfer function since the user has set on
  // explicitly (BUG #14371).
  this->setLockScalarRange(true);
  this->Internals->render();
  emit this->changeFinished();
  END_UNDO_SET();
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::invertTransferFunctions()
{
  BEGIN_UNDO_SET("Invert transfer function");
  vtkSMTransferFunctionProxy::InvertTransferFunction(this->proxy());

  emit this->changeFinished();
  // We don't invert the opacity function, for now.
  END_UNDO_SET();
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::choosePreset(const char* presetName)
{
  QAction* tmp = new QAction(NULL);
  pqChooseColorPresetReaction* ccpr = new pqChooseColorPresetReaction(tmp, false);
  ccpr->setTransferFunction(this->proxy());
  this->connect(ccpr, SIGNAL(presetApplied()), SLOT(presetApplied()));
  ccpr->choosePreset(presetName);
  delete ccpr;
  delete tmp;
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::presetApplied()
{
  emit this->changeFinished();

  // Assume the color map and opacity have changed and refresh
  this->Internals->ColorTableModel.refresh();
  this->Internals->OpacityTableModel.refresh();
}

//-----------------------------------------------------------------------------
void pqColorOpacityEditorWidget::saveAsPreset()
{
  QDialog dialog(this);
  Ui::SavePresetOptions ui;
  ui.setupUi(&dialog);
  ui.saveOpacities->setEnabled(this->scalarOpacityFunctionProxy() != NULL);
  ui.saveOpacities->setChecked(ui.saveOpacities->isEnabled());
  ui.saveAnnotations->setVisible(false);

  // For now, let's not provide an option to not save colors. We'll need to fix
  // the pqPresetToPixmap to support rendering only opacities.
  ui.saveColors->setChecked(true);
  ui.saveColors->setEnabled(false);
  ui.saveColors->hide();

  if (dialog.exec() != QDialog::Accepted)
    {
    return;
    }

  Q_ASSERT(ui.saveColors->isChecked());
  Json::Value preset = vtkSMTransferFunctionProxy::GetStateAsPreset(this->proxy());

  if (ui.saveOpacities->isChecked())
    {
    Json::Value opacities = vtkSMTransferFunctionProxy::GetStateAsPreset(
      this->scalarOpacityFunctionProxy());
    if (opacities.isMember("Points"))
      {
      preset["Points"] = opacities["Points"];
      }
    }

  vtkStdString presetName;
    {
    // This scoping is necessary to ensure that the vtkSMTransferFunctionPresets
    // saves the new preset to the "settings" before the choosePreset dialog is
    // shown.
    vtkNew<vtkSMTransferFunctionPresets> presets;
    presetName = presets->AddUniquePreset(preset);
    }
  this->choosePreset(presetName);
}
