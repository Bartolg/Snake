#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <chrono>
#include <memory>
#include <random>
#include <vector>

#include "Model.h"
#include "Shader.h"

struct android_app;

class Renderer {
public:
    /*!
     * @param pApp the android_app this Renderer belongs to, needed to configure GL
     */
    inline Renderer(android_app *pApp) :
            app_(pApp),
            display_(EGL_NO_DISPLAY),
            surface_(EGL_NO_SURFACE),
            context_(EGL_NO_CONTEXT),
            width_(0),
            height_(0),
            shaderNeedsNewProjectionMatrix_(true) {
        initRenderer();
    }

    virtual ~Renderer();

    /*!
     * Handles input from the android_app.
     *
     * Note: this will clear the input queue
     */
    void handleInput();

    /*!
     * Renders all the models in the renderer
     */
    void render();

private:
    struct Cell {
        int x;
        int y;
    };

    enum class Direction {
        Up,
        Down,
        Left,
        Right
    };

    /*!
     * Performs necessary OpenGL initialization. Customize this if you want to change your EGL
     * context or application-wide settings.
     */
    void initRenderer();

    /*!
     * @brief we have to check every frame to see if the framebuffer has changed in size. If it has,
     * update the viewport accordingly
     */
    void updateRenderArea();

    /*!
     * Creates the models for this sample. You'd likely load a scene configuration from a file or
     * use some other setup logic in your full game.
     */
    void createModels();

    void resetGame();
    void spawnFood();
    bool rebuildModels();
    void advanceSnake();
    bool advanceBotSnake(Direction &botDirection);
    Direction chooseBotDirection() const;
    Cell computeNextCell(const Cell &current, Direction direction) const;
    bool isCellOccupiedBySnake(const Cell &cell, const std::vector<Cell> &snake) const;
    void queueDirection(Direction direction);
    static bool isOpposite(Direction lhs, Direction rhs);
    void handleSwipe(float startX, float startY, float endX, float endY);

    android_app *app_;
    EGLDisplay display_;
    EGLSurface surface_;
    EGLContext context_;
    EGLint width_;
    EGLint height_;

    bool shaderNeedsNewProjectionMatrix_;

    std::unique_ptr<Shader> shader_;
    std::vector<Model> models_;

    int gridWidth_ = 100;
    int gridHeight_ = 100;
    std::vector<Cell> snake_;
    std::vector<Cell> botSnake_;
    Cell food_{};
    Direction direction_ = Direction::Right;
    Direction queuedDirection_ = Direction::Right;
    Direction botDirection_ = Direction::Left;
    std::shared_ptr<TextureAsset> snakeTexture_;
    std::shared_ptr<TextureAsset> foodTexture_;
    std::shared_ptr<TextureAsset> botTexture_;
    std::mt19937 randomEngine_;
    std::chrono::steady_clock::time_point lastFrameTime_;
    double timeAccumulator_ = 0.0;
    double moveInterval_ = 0.2;
    bool needsModelUpdate_ = true;
    bool touchActive_ = false;
    float touchStartX_ = 0.f;
    float touchStartY_ = 0.f;
};

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H