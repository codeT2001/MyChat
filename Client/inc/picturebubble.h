#ifndef PICTUREBUBBLE_H
#define PICTUREBUBBLE_H
#include "bubbleframe.h"

#include <QPixmap>
class PictureBubble : public BubbleFrame {
    Q_OBJECT
public:
    explicit PictureBubble(const QPixmap &pciture, bool self = true, QWidget *parent = nullptr);
};

#endif // PICTUREBUBBLE_H
