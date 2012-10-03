#include "apseekerwidget.h"
#include "ui_apseekerwidget.h"

#include <QtCore>
#include <QtGui>

#include "epsignal.h"
#include "epsignalscontroller.h"
#include "aputils.h"
#include "mainwindow.h"

bool APSeekerWidget::_running = false;

APSeekerWidget::APSeekerWidget(QWidget *parent) :
	QWidget(parent),
	ui(new Ui::APSeekerWidget)
{
	ui->setupUi(this);

	_globalProgress = 0;
	_numberOfSeekersThatDidFinish = 0;

	_progressBars = QMap<SegmentType, APProgressBar *>();
	_seekers = QMap<SegmentType, APSeeker *>();
	_stopButtons = QMap<SegmentType, QToolButton *>();

	connect(this, SIGNAL(progressDidChange(int)),
			ui->progressBar, SLOT(setValue(int)));
}

APSeekerWidget::~APSeekerWidget()
{
	delete ui;
}

void APSeekerWidget::beforeClosing()
{
	foreach (APSeeker *seeker, _seekers.values()) {
		seeker->dispatchProposedSegments();
	}

	EPSignalsController::activeSignal()->profile()->fixInnerCollisions();
	if (fillGaps)
		EPSignalsController::activeSignal()->profile()->fillGaps();

	EPSignalsController::activeSignal()->profile()->forceEmitOfSegmentsDidChange();

	APSeekerWidget::_running = false;
	ui->progressBar->setValue(ui->progressBar->maximum());

	showInfoMessage();
}

void APSeekerWidget::closeEvent(QCloseEvent *event)
{
	bool didAllSeekerFinish = _numberOfSeekersThatDidFinish == _seekers.values().length();
	if (didAllSeekerFinish) {
		beforeClosing();
		event->accept();
	} else {
		int response = QMessageBox::warning(this, tr("Cancel seekers"),
											tr("There are still some unfinished seekers.\n"
											   "If you continue, they will be stopped."),
											QMessageBox::Cancel | QMessageBox::Ok,
											QMessageBox::Cancel);
		if (response == QMessageBox::Ok) {
			stopAll();
			APSeekerWidget::_running = false;
			event->accept();
		} else {
			event->ignore();
		}
	}
}

void APSeekerWidget::dispatchSeekersForTypes(QList<SegmentType> types, bool shouldFillGaps)
{
	if (types.isEmpty()) return;

	APSeekerWidget *seekerWidget = new APSeekerWidget();
	seekerWidget->setWindowIcon(QIcon(":resources/logo.png"));
	EPSignal *signal = EPSignalsController::activeSignal();

	seekerWidget->fillGaps = shouldFillGaps;

	int row = 0;
	foreach (SegmentType type, types) {
		APSeeker *seeker = APSeeker::seekerForTypeAndSignal(type, signal, seekerWidget);
		connect(seeker, SIGNAL(workDidEnd(bool,QObject*,QString)),
				seekerWidget, SLOT(seekerDidEnd(bool,QObject*,QString)));
		connect(seeker, SIGNAL(progressDidChange(int)),
				seekerWidget, SLOT(seekerProgressDidChange(int)));
		connect(seeker, SIGNAL(workThrowsMessage(QString)),
				seekerWidget, SLOT(seekerDidThrowMessage(QString)));
		seekerWidget->_seekers.insert(type, seeker);

		APProgressBar *progressBar = new APProgressBar();
		progressBar->bindToWorker(seeker);
		progressBar->setMaximumHeight(14);
		progressBar->setTextVisible(false);
		seekerWidget->_progressBars.insert(type, progressBar);

		QToolButton *button = new QToolButton();
		button->setIcon(QIcon(":resources/stop.png"));
		button->setObjectName(APUtils::stringWithSegmentType(type));
		button->setDisabled(true);
		connect(button, SIGNAL(clicked()),
				seeker, SLOT(stop()));
		seekerWidget->_stopButtons.insert(type, button);

		QLabel *label = new QLabel("adasdasdasd");
		label->setHidden(true);
		label->setEnabled(false);
		QFont font = label->font();
		font.setPointSize(font.pointSize() - 1);
		label->setFont(font);
		seekerWidget->_labels.insert(type, label);

		seekerWidget->ui->grid->addWidget(new QLabel(APUtils::stringWithSegmentType(type)), row, 0);
		seekerWidget->ui->grid->addWidget(progressBar, row, 1);
		seekerWidget->ui->grid->addWidget(button, row, 2);
		seekerWidget->ui->grid->addWidget(label, row + 1, 1);

//		seeker->start();
		row += 2;
	}

	seekerWidget->adjustSize();
	seekerWidget->setMinimumHeight(seekerWidget->height());
	seekerWidget->setMaximumHeight(seekerWidget->height());

	seekerWidget->ui->progressBar->setMaximum(100 * types.length());
	seekerWidget->ui->title->setText(tr("Running %1").arg(signal->name()));

	APSeekerWidget::_running = true;
	seekerWidget->setWindowModality(Qt::ApplicationModal);
	seekerWidget->show();
}

void APSeekerWidget::seekerDidEnd(bool, QObject *, QString msg)
{
	SegmentType type = ((APSeeker *)sender())->type();
	_stopButtons.value(type)->setEnabled(false);
	_numberOfSeekersThatDidFinish++;
	_labels.value(type)->setText(msg);

	if (_numberOfSeekersThatDidFinish < _seekers.size()) {
		APSeeker *seeker = (APSeeker *)_seekers.values().at(_numberOfSeekersThatDidFinish);
		seeker->start();
		_stopButtons.value(seeker->type())->setEnabled(true);
	} else
		close();
}

void APSeekerWidget::seekerProgressDidChange(int progress)
{
	SegmentType type = ((APSeeker *)sender())->type();

//	if (type == Pd || type == Np) // pd goes from 0 to 100, since it's not sequential... so we need to transform it
//		_globalProgress += (progress - _progressBars[type]->value()) / 100.0
//				* EPSignalsController::activeSignal()->numberOfPoints();
//	else
	_globalProgress += progress - _progressBars[type]->value();
	emit progressDidChange(_globalProgress);
}

void APSeekerWidget::showEvent(QShowEvent *event)
{
	APSeeker *seeker = (APSeeker *)_seekers.values().at(_numberOfSeekersThatDidFinish);
	seeker->start();
	_stopButtons.value(seeker->type())->setEnabled(true);

	t.start();

	event->accept();
}

void APSeekerWidget::showInfoMessage()
{
	QString details;
	QTextStream stream(&details);

	int numberOfFoundSegments = 0;
	foreach (APSeeker *seeker, _seekers) {
		int count = seeker->numberOfProposedSegments();
		if (count > 0) {
			numberOfFoundSegments += count;
			stream << qSetFieldWidth(2 +2) << left << APUtils::stringWithSegmentType(seeker->type())
				   << qSetFieldWidth(11 +2) << tr("%1 segment%2").arg(count).arg(count > 1 ? "s" : "")
				   << reset << EPSignalsController::activeSignal()->profile()->totalDurationOfSegmentType(seeker->type()) << "s"
				   << "<br/>";
		} else {
			stream << qSetFieldWidth(2 +2) << left << APUtils::stringWithSegmentType(seeker->type())
				   << reset << "None found<br/>";
		}
	}

	int sec = t.elapsed() / 1000.0;
	bool greaterThanOneMinute = sec > 60;
	QString elapsed;
	if (greaterThanOneMinute) {
		elapsed = tr("%1 minute%2 %3 second%4")
				.arg(sec / 60)
				.arg(sec / 60 > 1 ? "s" : "")
				.arg(sec % 60)
				.arg(sec % 60 > 1 ? "s" : "");
	} else {
		elapsed = tr("%1 seconds").arg(sec);
	}

	QString message;
	if (numberOfFoundSegments > 0) {
		message = tr("<strong>%1</strong> segment%2 in <strong>%3</strong>:%4")
				.arg(numberOfFoundSegments)
				.arg(numberOfFoundSegments > 1 ? "s" : "")
				.arg(elapsed)
				.arg("<pre>" + details + "</pre>");
	} else
		message = "No segments found.";

	QMessageBox box(this);
	box.setWindowTitle("AutoEPG ~ Seeker Report");
	box.setIcon(QMessageBox::Information);
	box.setText("<strong>Search engines have finished successfully.</strong>");
	box.setInformativeText(message);
	box.setTextFormat(Qt::RichText);
	box.exec();
}

void APSeekerWidget::stopAll()
{
	foreach (APSeeker *seeker, _seekers.values()) {
		if (seeker->isRunning())
			seeker->stop();
	}
}

void APSeekerWidget::seekerDidThrowMessage(QString msg)
{
	SegmentType type = ((APSeeker *)sender())->type();
	QLabel *label = _labels.value(type);
	label->setText(msg);
	if (label->isHidden())
		label->show();

	setMaximumHeight(10000);
	adjustSize();
	setMinimumHeight(height());
	setMaximumHeight(height());
}