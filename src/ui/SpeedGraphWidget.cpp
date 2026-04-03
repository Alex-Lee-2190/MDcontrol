#include "MainWindow.h"
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>
#include <algorithm>

// --- SpeedGraphWidget Implementation ---

SpeedGraphWidget::SpeedGraphWidget(QWidget* parent) 
    : QWidget(parent), m_maxSpeed(1.0) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(120);
    
    // 初始化容量
    m_samples.reserve(410);
    
    // 内部状态控制（建议在头文件中声明这些变量，若不可改动头文件，此逻辑需在类中持久化）
    // 这里假设逻辑上我们需要：
    // static uint m_compressionFactor = 1; 
    // static QVector<double> m_pendingRaw;
}

// 为了保证代码完整性及逻辑闭环，在类中使用这些持久化变量
static uint g_compressionFactor = 1; 
static QVector<double> g_pendingRaw;

void SpeedGraphWidget::addSpeedSample(double bytesPerSec) {
    // 1. 将新采样存入临时采样区
    g_pendingRaw.push_back(bytesPerSec);
    
    // 2. 只有当累积的采样数达到当前的压缩倍率时，才向主显示数组存入一个峰值点
    // 这样确保了新旧曲线的“压缩次数”完全一致，视觉密度统一
    if (g_pendingRaw.size() >= (int)g_compressionFactor) {
        double maxInWindow = *std::max_element(g_pendingRaw.begin(), g_pendingRaw.end());
        m_samples.push_back(maxInWindow);
        g_pendingRaw.clear();
    }
    
    // 3. 当显示数组超过 400 个点时，进行数组减半压缩
    if (m_samples.size() >= 400) {
        QVector<double> compressed;
        compressed.reserve(200);
        for (int i = 0; i < m_samples.size() - 1; i += 2) {
            // 采用 Max 压缩法，保留曲线的尖峰特征，减少信息损耗
            compressed.append(std::max(m_samples[i], m_samples[i+1]));
        }
        m_samples = compressed;
        
        // 压缩倍率翻倍，确保后续新点按照新的比例对齐
        g_compressionFactor *= 2;
    }
    
    // 4. 动态 Y 轴缩放优化
    if (bytesPerSec > (m_maxSpeed / 1.1)) {
        m_maxSpeed = bytesPerSec * 1.1;
    } else if (m_samples.size() > 0 && m_samples.size() % 50 == 0) {
        // 降低校准频率，仅在数据点更新一定程度时重新计算最高点
        double m = 1024.0;
        for(double s : m_samples) if(s > m) m = s;
        m_maxSpeed = m * 1.1;
    }
    
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
    
    // 绘制网格
    p.setPen(gridColor);
    int gridW = width() / 10;
    int gridH = height() / 4;
    for(int i=1; i<10; ++i) p.drawLine(i*gridW, 0, i*gridW, height());
    for(int i=1; i<4; ++i) p.drawLine(0, i*gridH, width(), i*gridH);
    
    int n = m_samples.size();
    if (n < 2) return;
    
    double w = width();
    double h = height();
    
    // 准备渲染点集
    QVector<QPointF> points;
    points.reserve(n);
    
    for (int i = 0; i < n; ++i) {
        // 利用浮点数计算 X 坐标，实现亚像素平滑映射，彻底消除“纹理走样”现象
        double x = i * w / (n - 1);
        double y = h - (m_samples[i] / m_maxSpeed * h);
        points.append(QPointF(x, std::clamp(y, 0.0, h)));
    }

    // 绘制渐变填充区域
    QVector<QPointF> fillPoints = points;
    fillPoints.append(QPointF(points.last().x(), h));
    fillPoints.append(QPointF(0, h));
    
    QLinearGradient grad(0, 0, 0, h);
    grad.setColorAt(0, gradColor1);
    grad.setColorAt(1, gradColor2);
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawPolygon(fillPoints);
    
    // 绘制主速度折线
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(curveColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    // drawPolyline 的性能远高于 QPainterPath，适合高频刷新
    p.drawPolyline(points);
    
    if (!m_speedText.isEmpty()) {
        p.setPen(textColor);
        QFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(rect().adjusted(0, 0, -10, -10), Qt::AlignRight | Qt::AlignVCenter, m_speedText);
    }
}