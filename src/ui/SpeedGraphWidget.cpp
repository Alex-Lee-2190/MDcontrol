#include "MainWindow.h"
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>

// --- SpeedGraphWidget Implementation ---

SpeedGraphWidget::SpeedGraphWidget(QWidget* parent) : QWidget(parent), m_maxSpeed(1.0) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(120);
}

void SpeedGraphWidget::addSpeedSample(double bytesPerSec) {
    m_samples.push_back(bytesPerSec);
    
    double max = 1024.0; 
    
    int startIdx = 0;
    if (m_samples.size() > 20) {
        startIdx = 10;
    }

    for(int i = startIdx; i < m_samples.size(); ++i) {
        if(m_samples[i] > max) max = m_samples[i];
    }
    
    m_maxSpeed = max * 1.1; 
    update();
}

void SpeedGraphWidget::setCurrentSpeedText(const QString& text) {
    m_speedText = text;
    update();
}

void SpeedGraphWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    bool isDark = (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    QColor bgColor = isDark ? QColor(30, 30, 30) : Qt::white;
    QColor gridColor = isDark ? QColor(60, 60, 60) : QColor(230, 230, 230);
    QColor curveColor = isDark ? QColor(0, 120, 215) : QColor(0, 180, 0);
    QColor gradColor1 = isDark ? QColor(0, 120, 215, 150) : QColor(0, 200, 0, 150);
    QColor gradColor2 = isDark ? QColor(0, 120, 215, 50) : QColor(0, 200, 0, 50);
    QColor textColor = isDark ? Qt::white : Qt::black;

    p.fillRect(rect(), bgColor);
    p.setPen(gridColor);
    int gridW = width() / 10;
    int gridH = height() / 4;
    for(int i=1; i<10; ++i) p.drawLine(i*gridW, 0, i*gridW, height());
    for(int i=1; i<4; ++i) p.drawLine(0, i*gridH, width(), i*gridH);
    
    if (m_samples.size() < 2) return;
    
    QPainterPath path;
    path.moveTo(0, height()); 
    
    double xStep = (double)width() / (m_samples.size() - 1);
    
    for(int i=0; i<m_samples.size(); ++i) {
        double val = m_samples[i];
        double y = height() - (val / m_maxSpeed * height());
        if (y < 0) y = 0; 
        path.lineTo(i * xStep, y);
    }
    
    path.lineTo(width(), height()); 
    path.closeSubpath();
    
    QLinearGradient grad(0, 0, 0, height());
    grad.setColorAt(0, gradColor1);
    grad.setColorAt(1, gradColor2);
    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    p.drawPath(path);
    
    p.setPen(QPen(curveColor, 2));
    p.setBrush(Qt::NoBrush);
    QPainterPath strokePath;
    
    double firstY = height() - (m_samples[0] / m_maxSpeed * height());
    if (firstY < 0) firstY = 0;
    strokePath.moveTo(0, firstY);

    for(int i=1; i<m_samples.size(); ++i) {
        double y = height() - (m_samples[i] / m_maxSpeed * height());
        if (y < 0) y = 0;
        strokePath.lineTo(i * xStep, y);
    }
    p.drawPath(strokePath);
    
    if (!m_speedText.isEmpty()) {
        p.setPen(textColor);
        QFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(rect().adjusted(0, 0, -10, -10), Qt::AlignRight | Qt::AlignVCenter, m_speedText);
    }
}