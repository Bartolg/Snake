#ifndef ANDROIDGLINVESTIGATIONS_TEXTUREASSET_H
#define ANDROIDGLINVESTIGATIONS_TEXTUREASSET_H

#include <android/asset_manager.h>
#include <GLES3/gl3.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class TextureAsset {
public:
    /*!
     * Loads a texture asset from the assets/ directory
     * @param assetManager Asset manager to use
     * @param assetPath The path to the asset
     * @return a shared pointer to a texture asset, resources will be reclaimed when it's cleaned up
     */
    static std::shared_ptr<TextureAsset>
    loadAsset(AAssetManager *assetManager, const std::string &assetPath);

    /*!
     * Creates a 1x1 texture filled with the supplied color.
     *
     * @param red red channel (0-255)
     * @param green green channel (0-255)
     * @param blue blue channel (0-255)
     * @param alpha alpha channel (0-255)
     * @return a shared pointer to a texture asset, resources will be reclaimed when it's cleaned up
     */
    static std::shared_ptr<TextureAsset>
    createSolidColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 0xFF);

    ~TextureAsset();

    /*!
     * @return the texture id for use with OpenGL
     */
    constexpr GLuint getTextureID() const { return textureID_; }

private:
    inline TextureAsset(GLuint textureId) : textureID_(textureId) {}

    GLuint textureID_;
};

#endif //ANDROIDGLINVESTIGATIONS_TEXTUREASSET_H