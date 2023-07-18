#ifndef LAVA_STRVIEW_H
#define LAVA_STRVIEW_H

#include <stdlib.h>
#include "util.h"

typedef struct StrView {
    char* start;
    size_t len;
} StrView;

StrView* initStrView(char* start, size_t len) {
    StrView* view = MALLOC(sizeof(StrView));
    view->start = start;
    view->len = len;
    return view;
}

void printView(StrView* view, char* suffix) {
    printf("%.*s%s", (int) view->len, view->start, suffix);
}

char* viewToStr(StrView* view) {
    char* str = MALLOC((view->len + 1) * sizeof(char));
    strncpy(str, view->start, view->len);
    str[view->len] = '\0';
    return str;
}
#endif //LAVA_STRVIEW_H