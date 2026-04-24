#pragma once

void scanImages();
void displayImageFromFile(int slot);
void loadSavedImage();
void nextImage();

// 动态图库：返回已存在图片槽位列表（slot id），以及下一个可用空槽位（若无则返回 -1）
int image_next_free_slot();
bool image_has_slot(int slot);
