#include "Renderer.h"

#include <android/keycodes.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"
#include "TextureAsset.h"

//! executes glGetString and outputs the result to logcat
#define PRINT_GL_STRING(s) {aout << #s": "<< glGetString(s) << std::endl;}

/*!
 * @brief if glGetString returns a space separated list of elements, prints each one on a new line
 *
 * This works by creating an istringstream of the input c-style string. Then that is used to create
 * a vector -- each element of the vector is a new element in the input string. Finally a foreach
 * loop consumes this and outputs it to logcat using @a aout
 */
#define PRINT_GL_STRING_AS_LIST(s) { \
std::istringstream extensionStream((const char *) glGetString(s));\
std::vector<std::string> extensionList(\
        std::istream_iterator<std::string>{extensionStream},\
        std::istream_iterator<std::string>());\
aout << #s":\n";\
for (auto& extension: extensionList) {\
    aout << extension << "\n";\
}\
aout << std::endl;\
}

//! Color for cornflower blue. Can be sent directly to glClearColor
#define CORNFLOWER_BLUE 100 / 255.f, 149 / 255.f, 237 / 255.f, 1

// Vertex shader, you'd typically load this from assets
static const char *vertex = R"vertex(#version 300 es
in vec3 inPosition;
in vec2 inUV;

out vec2 fragUV;

uniform mat4 uProjection;

void main() {
    fragUV = inUV;
    gl_Position = uProjection * vec4(inPosition, 1.0);
}
)vertex";

// Fragment shader, you'd typically load this from assets
static const char *fragment = R"fragment(#version 300 es
precision mediump float;

in vec2 fragUV;

uniform sampler2D uTexture;

out vec4 outColor;

void main() {
    outColor = texture(uTexture, fragUV);
}
)fragment";

/*!
 * Half the height of the projection matrix. This gives you a renderable area of height 4 ranging
 * from -2 to 2
 */
static constexpr float kProjectionHalfHeight = 2.f;

/*!
 * The near plane distance for the projection matrix. Since this is an orthographic projection
 * matrix, it's convenient to have negative values for sorting (and avoiding z-fighting at 0).
 */
static constexpr float kProjectionNearPlane = -1.f;

/*!
 * The far plane distance for the projection matrix. Since this is an orthographic porjection
 * matrix, it's convenient to have the far plane equidistant from 0 as the near plane.
 */
static constexpr float kProjectionFarPlane = 1.f;

Renderer::~Renderer() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
}

void Renderer::render() {
    updateRenderArea();

    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration<double>(now - lastFrameTime_).count();
    lastFrameTime_ = now;
    timeAccumulator_ += delta;

    while (timeAccumulator_ >= moveInterval_) {
        timeAccumulator_ -= moveInterval_;
        advanceSnake();
    }

    if (needsModelUpdate_) {
        if (rebuildModels()) {
            needsModelUpdate_ = false;
        }
    }

    if (shaderNeedsNewProjectionMatrix_ && height_ > 0) {
        float projectionMatrix[16] = {0};
        const float aspect = static_cast<float>(std::max<EGLint>(width_, 1))
                / static_cast<float>(height_);
        Utility::buildOrthographicMatrix(
                projectionMatrix,
                kProjectionHalfHeight,
                aspect,
                kProjectionNearPlane,
                kProjectionFarPlane);
        shader_->setProjectionMatrix(projectionMatrix);
        shaderNeedsNewProjectionMatrix_ = false;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    for (const auto &model: models_) {
        shader_->drawModel(model);
    }

    auto swapResult = eglSwapBuffers(display_, surface_);
    assert(swapResult == EGL_TRUE);
}

void Renderer::initRenderer() {
    // Choose your render attributes
    constexpr EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    // The default display is probably what you want on Android
    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);

    // figure out how many configs there are
    EGLint numConfigs;
    eglChooseConfig(display, attribs, nullptr, 0, &numConfigs);

    // get the list of configurations
    std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
    eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs, &numConfigs);

    // Find a config we like.
    // Could likely just grab the first if we don't care about anything else in the config.
    // Otherwise hook in your own heuristic
    auto config = *std::find_if(
            supportedConfigs.get(),
            supportedConfigs.get() + numConfigs,
            [&display](const EGLConfig &config) {
                EGLint red, green, blue, depth;
                if (eglGetConfigAttrib(display, config, EGL_RED_SIZE, &red)
                    && eglGetConfigAttrib(display, config, EGL_GREEN_SIZE, &green)
                    && eglGetConfigAttrib(display, config, EGL_BLUE_SIZE, &blue)
                    && eglGetConfigAttrib(display, config, EGL_DEPTH_SIZE, &depth)) {

                    aout << "Found config with " << red << ", " << green << ", " << blue << ", "
                         << depth << std::endl;
                    return red == 8 && green == 8 && blue == 8 && depth == 24;
                }
                return false;
            });

    aout << "Found " << numConfigs << " configs" << std::endl;
    aout << "Chose " << config << std::endl;

    // create the proper window surface
    EGLint format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    EGLSurface surface = eglCreateWindowSurface(display, config, app_->window, nullptr);

    // Create a GLES 3 context
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);

    // get some window metrics
    auto madeCurrent = eglMakeCurrent(display, surface, surface, context);
    assert(madeCurrent);

    display_ = display;
    surface_ = surface;
    context_ = context;

    // make width and height invalid so it gets updated the first frame in @a updateRenderArea()
    width_ = -1;
    height_ = -1;

    PRINT_GL_STRING(GL_VENDOR);
    PRINT_GL_STRING(GL_RENDERER);
    PRINT_GL_STRING(GL_VERSION);
    PRINT_GL_STRING_AS_LIST(GL_EXTENSIONS);

    shader_ = std::unique_ptr<Shader>(
            Shader::loadShader(vertex, fragment, "inPosition", "inUV", "uProjection"));
    assert(shader_);

    // Note: there's only one shader in this demo, so I'll activate it here. For a more complex game
    // you'll want to track the active shader and activate/deactivate it as necessary
    shader_->activate();

    // setup any other gl related global states
    glClearColor(CORNFLOWER_BLUE);

    // enable alpha globally for now, you probably don't want to do this in a game
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // get some demo models into memory
    createModels();
}

void Renderer::updateRenderArea() {
    EGLint width;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);

    EGLint height;
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);

    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        glViewport(0, 0, width, height);

        // make sure that we lazily recreate the projection matrix before we render
        shaderNeedsNewProjectionMatrix_ = true;
    }
}

/**
 * @brief Create any demo models we want for this demo.
 */
void Renderer::createModels() {
    auto *assetManager = app_ && app_->activity ? app_->activity->assetManager : nullptr;
    if (assetManager) {
        snakeTexture_ = TextureAsset::loadAsset(assetManager, "snake.png");
    }
    if (!snakeTexture_) {
        snakeTexture_ = TextureAsset::createSolidColor(0x4C, 0xAF, 0x50);
    }
    foodTexture_ = TextureAsset::createSolidColor(0xFF, 0x57, 0x22);
    botTexture_ = TextureAsset::createSolidColor(0x21, 0x96, 0xF3);

    std::random_device rd;
    randomEngine_.seed(rd());

    resetGame();
}

void Renderer::resetGame() {
    snake_.clear();
    botSnake_.clear();
    const int startX = gridWidth_ / 2;
    const int startY = gridHeight_ / 2;
    snake_.push_back({startX, startY});
    snake_.push_back({startX - 1, startY});
    snake_.push_back({startX - 2, startY});

    botSnake_.push_back({startX, startY + 3});
    botSnake_.push_back({startX + 1, startY + 3});
    botSnake_.push_back({startX + 2, startY + 3});

    direction_ = Direction::Right;
    queuedDirection_ = direction_;
    botDirection_ = Direction::Left;
    timeAccumulator_ = 0.0;
    lastFrameTime_ = std::chrono::steady_clock::now();

    food_.clear();
    spawnFood();
    needsModelUpdate_ = true;
}

void Renderer::spawnFood() {
    if (food_.size() >= targetFoodCount_) {
        return;
    }

    std::vector<Cell> emptyCells;
    emptyCells.reserve(static_cast<size_t>(gridWidth_ * gridHeight_));
    for (int y = 0; y < gridHeight_; ++y) {
        for (int x = 0; x < gridWidth_; ++x) {
            const bool occupied = std::any_of(
                    snake_.begin(),
                    snake_.end(),
                    [x, y](const Cell &segment) {
                        return segment.x == x && segment.y == y;
                    }) || std::any_of(
                    botSnake_.begin(),
                    botSnake_.end(),
                    [x, y](const Cell &segment) {
                        return segment.x == x && segment.y == y;
                    }) || std::any_of(
                    food_.begin(),
                    food_.end(),
                    [x, y](const Cell &existingFood) {
                        return existingFood.x == x && existingFood.y == y;
                    });
            if (!occupied) {
                emptyCells.push_back({x, y});
            }
        }
    }

    if (emptyCells.empty()) {
        resetGame();
        return;
    }

    std::shuffle(emptyCells.begin(), emptyCells.end(), randomEngine_);
    const size_t spawnCount = std::min(emptyCells.size(), targetFoodCount_ - food_.size());
    for (size_t i = 0; i < spawnCount; ++i) {
        food_.push_back(emptyCells[i]);
    }

    if (spawnCount > 0) {
        needsModelUpdate_ = true;
    }
}

bool Renderer::rebuildModels() {
    if (!snakeTexture_ || !foodTexture_) {
        return false;
    }

    if (width_ <= 0 || height_ <= 0) {
        return false;
    }

    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float worldHeight = kProjectionHalfHeight * 2.f;
    const float worldWidth = worldHeight * aspect;
    const float cellWidth = worldWidth / static_cast<float>(gridWidth_);
    const float cellHeight = worldHeight / static_cast<float>(gridHeight_);
    const float minX = -worldWidth / 2.f;
    const float minY = -worldHeight / 2.f;

    models_.clear();
    models_.reserve(snake_.size() + botSnake_.size() + food_.size());

    struct UVRect {
        float u0;
        float v0;
        float u1;
        float v1;
    };

    auto appendQuad = [&](const Cell &cell,
                          const std::shared_ptr<TextureAsset> &texture,
                          const UVRect &uvRect) {
        const float centerX = minX + (static_cast<float>(cell.x) + 0.5f) * cellWidth;
        const float centerY = minY + (static_cast<float>(cell.y) + 0.5f) * cellHeight;
        const float halfWidth = cellWidth / 2.f;
        const float halfHeight = cellHeight / 2.f;

        std::vector<Vertex> vertices = {
                Vertex(Vector3{centerX + halfWidth, centerY + halfHeight, 0.f},
                       Vector2{uvRect.u0, uvRect.v0}),
                Vertex(Vector3{centerX - halfWidth, centerY + halfHeight, 0.f},
                       Vector2{uvRect.u1, uvRect.v0}),
                Vertex(Vector3{centerX - halfWidth, centerY - halfHeight, 0.f},
                       Vector2{uvRect.u1, uvRect.v1}),
                Vertex(Vector3{centerX + halfWidth, centerY - halfHeight, 0.f},
                       Vector2{uvRect.u0, uvRect.v1}),
        };
        std::vector<Index> indices = {0, 1, 2, 0, 2, 3};
        models_.emplace_back(std::move(vertices), std::move(indices), texture);
    };

    const UVRect fullTexture{0.f, 0.f, 1.f, 1.f};
    const std::array<UVRect, 4> snakeUVs{{
            {0.f, 0.f, 0.25f, 1.f},
            {0.25f, 0.f, 0.5f, 1.f},
            {0.5f, 0.f, 0.75f, 1.f},
            {0.75f, 0.f, 1.f, 1.f},
    }};

    for (size_t index = 0; index < snake_.size(); ++index) {
        const auto &segment = snake_[index];
        UVRect uvRect = fullTexture;
        if (snakeTexture_) {
            if (index == 0) {
                uvRect = snakeUVs[0];
            } else {
                const size_t bodyIndex = 1 + ((index - 1) % (snakeUVs.size() - 1));
                uvRect = snakeUVs[bodyIndex];
            }
        }
        appendQuad(segment, snakeTexture_, uvRect);
    }

    for (const auto &segment : botSnake_) {
        appendQuad(segment, botTexture_, fullTexture);
    }

    for (const auto &foodCell : food_) {
        appendQuad(foodCell, foodTexture_, fullTexture);
    }
    return true;
}

void Renderer::advanceSnake() {
    if (snake_.empty() || botSnake_.empty()) {
        return;
    }

    if (!isOpposite(queuedDirection_, direction_) || snake_.size() <= 1) {
        direction_ = queuedDirection_;
    }

    if (!advanceBotSnake(botDirection_)) {
        return;
    }

    Cell newHead = computeNextCell(snake_.front(), direction_);

    const bool hitSelf = isCellOccupiedBySnake(newHead, snake_);
    const bool hitBot = isCellOccupiedBySnake(newHead, botSnake_);

    if (hitSelf || hitBot) {
        resetGame();
        return;
    }

    snake_.insert(snake_.begin(), newHead);

    bool playerAteFood = false;
    for (auto it = food_.begin(); it != food_.end();) {
        if (newHead.x == it->x && newHead.y == it->y) {
            it = food_.erase(it);
            playerAteFood = true;
        } else {
            ++it;
        }
    }

    if (!playerAteFood) {
        snake_.pop_back();
    }

    bool botAteFood = false;
    if (!botSnake_.empty()) {
        const auto &botHead = botSnake_.front();
        for (auto it = food_.begin(); it != food_.end();) {
            if (botHead.x == it->x && botHead.y == it->y) {
                it = food_.erase(it);
                botAteFood = true;
            } else {
                ++it;
            }
        }

        if (!botAteFood) {
            botSnake_.pop_back();
        }

        if (botHead.x == newHead.x && botHead.y == newHead.y) {
            resetGame();
            return;
        }
    }

    if (playerAteFood || botAteFood) {
        spawnFood();
    }

    needsModelUpdate_ = true;
}

bool Renderer::advanceBotSnake(Direction &botDirection) {
    if (botSnake_.empty()) {
        return true;
    }

    if (botSnake_.size() <= 1) {
        botDirection = chooseBotDirection();
    } else {
        const Direction desired = chooseBotDirection();
        if (!isOpposite(desired, botDirection)) {
            botDirection = desired;
        }
    }

    Cell newHead = computeNextCell(botSnake_.front(), botDirection);

    const bool hitSelf = isCellOccupiedBySnake(newHead, botSnake_);
    const bool hitPlayer = isCellOccupiedBySnake(newHead, snake_);
    if (hitSelf || hitPlayer) {
        resetGame();
        return false;
    }

    botSnake_.insert(botSnake_.begin(), newHead);
    return true;
}

Renderer::Direction Renderer::chooseBotDirection() const {
    if (botSnake_.empty()) {
        return botDirection_;
    }

    const std::array<Direction, 4> directions{
            Direction::Up,
            Direction::Down,
            Direction::Left,
            Direction::Right,
    };

    const auto head = botSnake_.front();
    Direction bestDirection = botDirection_;
    int bestDistance = std::numeric_limits<int>::max();

    for (const auto direction : directions) {
        if (botSnake_.size() > 1 && isOpposite(direction, botDirection_)) {
            continue;
        }

        const Cell nextCell = computeNextCell(head, direction);
        if (isCellOccupiedBySnake(nextCell, botSnake_)) {
            continue;
        }
        if (isCellOccupiedBySnake(nextCell, snake_)) {
            continue;
        }

        int distance = std::numeric_limits<int>::max();
        if (!food_.empty()) {
            for (const auto &foodCell : food_) {
                int dx = std::abs(nextCell.x - foodCell.x);
                int dy = std::abs(nextCell.y - foodCell.y);
                if (gridWidth_ > 0) {
                    dx = std::min(dx, gridWidth_ - dx);
                }
                if (gridHeight_ > 0) {
                    dy = std::min(dy, gridHeight_ - dy);
                }
                distance = std::min(distance, dx + dy);
            }
        } else {
            distance = 0;
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            bestDirection = direction;
        }
    }

    return bestDirection;
}

Renderer::Cell Renderer::computeNextCell(const Cell &current, Direction direction) const {
    Cell next = current;
    switch (direction) {
        case Direction::Up:
            next.y += 1;
            break;
        case Direction::Down:
            next.y -= 1;
            break;
        case Direction::Left:
            next.x -= 1;
            break;
        case Direction::Right:
            next.x += 1;
            break;
    }

    if (gridWidth_ > 0) {
        if (next.x < 0) {
            next.x = gridWidth_ - 1;
        } else if (next.x >= gridWidth_) {
            next.x = 0;
        }
    }

    if (gridHeight_ > 0) {
        if (next.y < 0) {
            next.y = gridHeight_ - 1;
        } else if (next.y >= gridHeight_) {
            next.y = 0;
        }
    }

    return next;
}

bool Renderer::isCellOccupiedBySnake(const Cell &cell, const std::vector<Cell> &snake) const {
    return std::any_of(
            snake.begin(),
            snake.end(),
            [&cell](const Cell &segment) {
                return segment.x == cell.x && segment.y == cell.y;
            });
}

void Renderer::queueDirection(Direction direction) {
    if (!isOpposite(direction, direction_) || snake_.size() <= 1) {
        queuedDirection_ = direction;
    }
}

bool Renderer::isOpposite(Direction lhs, Direction rhs) {
    return (lhs == Direction::Up && rhs == Direction::Down)
           || (lhs == Direction::Down && rhs == Direction::Up)
           || (lhs == Direction::Left && rhs == Direction::Right)
           || (lhs == Direction::Right && rhs == Direction::Left);
}

void Renderer::handleSwipe(float startX, float startY, float endX, float endY) {
    float dx = endX - startX;
    float dy = endY - startY;

    const float minDistance = 16.f;
    if (std::fabs(dx) < minDistance && std::fabs(dy) < minDistance) {
        if (width_ > 0 && height_ > 0) {
            const float centerX = static_cast<float>(width_) / 2.f;
            const float centerY = static_cast<float>(height_) / 2.f;
            dx = endX - centerX;
            dy = endY - centerY;
        }
    }

    if (std::fabs(dx) > std::fabs(dy)) {
        queueDirection(dx > 0 ? Direction::Right : Direction::Left);
    } else {
        queueDirection(dy > 0 ? Direction::Down : Direction::Up);
    }
}

void Renderer::handleInput() {
    auto *inputBuffer = android_app_swap_input_buffers(app_);
    if (!inputBuffer) {
        return;
    }

    for (auto i = 0; i < inputBuffer->motionEventsCount; i++) {
        auto &motionEvent = inputBuffer->motionEvents[i];
        if (motionEvent.pointerCount == 0) {
            continue;
        }
        const auto action = motionEvent.action & AMOTION_EVENT_ACTION_MASK;
        const size_t pointerCount = static_cast<size_t>(motionEvent.pointerCount);
        auto pointerIndex = static_cast<size_t>(
                (motionEvent.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        pointerIndex = std::min(pointerIndex, pointerCount - 1);
        const auto &pointer = motionEvent.pointers[pointerIndex];
        const float x = GameActivityPointerAxes_getX(&pointer);
        const float y = GameActivityPointerAxes_getY(&pointer);

        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                touchActive_ = true;
                touchStartX_ = x;
                touchStartY_ = y;
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                if (touchActive_) {
                    handleSwipe(touchStartX_, touchStartY_, x, y);
                    touchActive_ = false;
                }
                break;
            case AMOTION_EVENT_ACTION_CANCEL:
                touchActive_ = false;
                break;
            default:
                break;
        }
    }
    android_app_clear_motion_events(inputBuffer);

    for (auto i = 0; i < inputBuffer->keyEventsCount; i++) {
        auto &keyEvent = inputBuffer->keyEvents[i];
        switch (keyEvent.action) {
            case AKEY_EVENT_ACTION_DOWN:
                switch (keyEvent.keyCode) {
                    case AKEYCODE_DPAD_UP:
                    case AKEYCODE_W:
                        queueDirection(Direction::Up);
                        break;
                    case AKEYCODE_DPAD_DOWN:
                    case AKEYCODE_S:
                        queueDirection(Direction::Down);
                        break;
                    case AKEYCODE_DPAD_LEFT:
                    case AKEYCODE_A:
                        queueDirection(Direction::Left);
                        break;
                    case AKEYCODE_DPAD_RIGHT:
                    case AKEYCODE_D:
                        queueDirection(Direction::Right);
                        break;
                    case AKEYCODE_ENTER:
                    case AKEYCODE_SPACE:
                        resetGame();
                        break;
                    default:
                        break;
                }
                break;
            case AKEY_EVENT_ACTION_UP:
            case AKEY_EVENT_ACTION_MULTIPLE:
            default:
                break;
        }
    }
    android_app_clear_key_events(inputBuffer);
}