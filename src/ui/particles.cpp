#include "ui/particles.h"

#include "raylib.h"

#include <cmath>
#include <random>

namespace {
struct Spark {
    ImVec2 position, velocity;
    ImU32 color = 0;
    float life = 0.0f, lived = 0.0f, size = 0.0f;
};
const int MOST_SPARKS = 384;
struct SparkField {
    Spark sparks[MOST_SPARKS];
    int next = 0;
    std::mt19937 random{ 7 };
};
}
static SparkField field;

void spawnBurst(ImVec2 at, ImU32 color, int count, float speed, float scale){
    std::uniform_real_distribution<float> angle(0.0f, 6.2831853f), spread(0.4f, 1.0f), life(0.35f, 0.7f), size(1.5f, 3.5f);
    for (int i = 0; i < count; i++){
        Spark& spark = field.sparks[field.next];
        field.next = (field.next + 1) % MOST_SPARKS;
        const float a = angle(field.random), v = speed * spread(field.random) * scale;
        spark.position = at;
        spark.velocity = ImVec2(std::cos(a) * v, std::sin(a) * v - 60.0f * scale);
        spark.color = color;
        spark.life = life(field.random);
        spark.lived = 0.0f;
        spark.size = size(field.random) * scale;
    }
}

void drawParticles(float scale){
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float dt = std::min(GetFrameTime(), 0.05f);
    for (Spark& spark : field.sparks){
        if (spark.lived >= spark.life) continue;
        spark.lived += dt;
        spark.velocity.y += 420.0f * scale * dt; // falling a little
        spark.velocity.x *= 1.0f - 2.5f * dt;    // and slowing
        spark.position.x += spark.velocity.x * dt;
        spark.position.y += spark.velocity.y * dt;
        const float left = 1.0f - spark.lived / spark.life;
        if (left <= 0.0f) continue;
        const ImU32 alpha = (ImU32)(((spark.color >> IM_COL32_A_SHIFT) & 0xFF) * left);
        draw->AddCircleFilled(spark.position, spark.size * (0.5f + 0.5f * left), (spark.color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT), 8);
    }
}
