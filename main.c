#include "raylib.h"
#include <stdio.h>

#define WINDOW_HEIGHT GetScreenHeight()
#define WINDOW_WIDTH GetScreenWidth()
#define G_CLEF_CODEPOINT 0xE050

float fontSize = 100;
int staffWidth = 30;

int musicCodepoints[] = { 
        G_CLEF_CODEPOINT, // G-clef
        0xE05C, // C-clef
        0xE062  // F-clef
    }; 

float topStaffPercent = 3.0f/32.0f;
float bottomStaffPercent = 10.85f/32.0f;

void drawStaff(Font bravura, Vector2 position, int staffTopPosY, int staffWidth){
    DrawTextCodepoint(bravura, G_CLEF_CODEPOINT,position, fontSize, BLACK );
    // printf("staffTopPosY %d staffBottomPosY %d staffWidth %d", staffTopPosY,staffBottomPosY,staffWidth);
    for (int i=0; i < 5; i++){
        
        DrawLine((int)position.x, (int)staffTopPosY+i*staffWidth, (int)position.x+800, (int)staffTopPosY+i*staffWidth, BLACK);
    }
}

int main(void){
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "HELLO WORLD!");
    // SetTextureFilter(bravura.texture, TEXTURE_FILTER_BILINEAR);
    Font bravura = LoadFontEx("Ressources/Bravura.otf", (int)fontSize, musicCodepoints, 3);
    
    printf("topStaffPercent %f bottomStaffPercent %f",topStaffPercent,bottomStaffPercent);
    Vector2 staffPosition = {200,300};
    printf("bravura glyph offsetY%d\n",bravura.glyphs->offsetY);
    printf("bravura glyph offsetX%d\n",bravura.glyphs->offsetX);
    int staffTopPosY = (int)staffPosition.y+bravura.glyphs->offsetY + (int)(fontSize*topStaffPercent);
    int staffBottomPosY = (int)staffPosition.y+bravura.glyphs->offsetY + (int)(fontSize*bottomStaffPercent);
    int staffWidth = (staffBottomPosY - staffTopPosY)/4;
    SetTargetFPS(60);
    RenderTexture2D staffTexture = LoadRenderTexture(1000, 600);
    BeginTextureMode(staffTexture);
    drawStaff(bravura,staffPosition, staffTopPosY, staffWidth);

    EndTextureMode();
    while(!WindowShouldClose()){
        
        printf("FPS: %d\n", GetFPS());
        BeginDrawing();
        ClearBackground(RAYWHITE);
        Rectangle source = {0, 0, (float)staffTexture.texture.width, (float)-staffTexture.texture.height};
        DrawTextureRec(staffTexture.texture,  source, (Vector2){0,0} , WHITE);
        DrawText("Raylib + Bravura Staff Demo", 10, 10, 20, DARKGRAY);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}