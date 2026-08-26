--[[
  Alpha Bleed & Export — for Wind Waker-style cloud textures.

  Aseprite's eraser leaves fully-transparent pixels as transparent BLACK (RGB 0,0,0). The game draws
  clouds with bilinear filtering and non-premultiplied alpha, so it blends the colour channels toward
  that hidden black and you get a dark outline around every cloud. This script floods the cloud colour
  outward into the transparent region and writes the PNG. The alpha channel is never touched, so the
  edges you drew are exactly the edges you get.

  Install: File > Scripts > Open Scripts Folder, drop this in, then File > Scripts > Rescan Scripts Folder.
  Use:     open your cloud .aseprite and run File > Scripts > aseprite-ww-cloud-alpha-bleed.
           It writes <same name>.png next to it, with all layers flattened. Your sprite is left alone.

  Run this LAST. Aseprite's own Layer > Flatten zeroes the colour under transparent pixels again, so
  bleeding has to be the final step before export.

  Only frame 1 is exported — cloud textures are single images.
]]

-- The two horizon strips tile left-to-right, so their bleed wraps in X; sprites clamp.
local WRAP_X = { cloud_mae = true, cloud_naka = true }

local sprite = app.sprite
if not sprite then
    app.alert("Open a sprite first.")
    return
end
if sprite.colorMode ~= ColorMode.RGB then
    app.alert("This sprite is " .. tostring(sprite.colorMode) .. " — switch to RGB (Sprite > Color Mode > RGB).")
    return
end
if sprite.filename == "" then
    app.alert("Save the sprite first — the PNG is written next to it, under the same name.")
    return
end

local image = Image(sprite) -- flattened render of frame 1
local w, h = image.width, image.height
local basename = app.fs.fileTitle(sprite.filename)
local wrapX = WRAP_X[basename] or false

-- Unpack into flat Lua arrays (1-based, row-major) — far faster than per-pixel API calls in the loop.
local pc = app.pixelColor
local r, g, b, a, known = {}, {}, {}, {}, {}
for y = 0, h - 1 do
    for x = 0, w - 1 do
        local i = y * w + x + 1
        local px = image:getPixel(x, y)
        r[i], g[i], b[i], a[i] = pc.rgbaR(px), pc.rgbaG(px), pc.rgbaB(px), pc.rgbaA(px)
        known[i] = a[i] > 0
    end
end

-- Index of the neighbour at (x+dx, y+dy), or nil if it falls outside the image.
local function neighbour(x, y, dx, dy)
    local nx, ny = x + dx, y + dy
    if wrapX then
        nx = nx % w
    elseif nx < 0 or nx >= w then
        return nil
    end
    if ny < 0 or ny >= h then
        return nil
    end
    return ny * w + nx + 1
end

-- Dilate: each pass fills unknown pixels from the mean of their known neighbours, until none are left.
local recoloured = 0
while true do
    local fillR, fillG, fillB, fillAt = {}, {}, {}, {}
    for y = 0, h - 1 do
        for x = 0, w - 1 do
            local i = y * w + x + 1
            if not known[i] then
                local sr, sg, sb, n = 0, 0, 0, 0
                for dy = -1, 1 do
                    for dx = -1, 1 do
                        if dx ~= 0 or dy ~= 0 then
                            local j = neighbour(x, y, dx, dy)
                            if j and known[j] then
                                sr, sg, sb, n = sr + r[j], sg + g[j], sb + b[j], n + 1
                            end
                        end
                    end
                end
                if n > 0 then
                    local k = #fillAt + 1
                    fillAt[k], fillR[k], fillG[k], fillB[k] = i, sr / n, sg / n, sb / n
                end
            end
        end
    end
    if #fillAt == 0 then
        break -- done, or the image is entirely transparent with nothing to bleed from
    end
    for k = 1, #fillAt do
        local i = fillAt[k]
        r[i], g[i], b[i], known[i] = math.floor(fillR[k] + 0.5), math.floor(fillG[k] + 0.5), math.floor(fillB[k] + 0.5), true
    end
    recoloured = recoloured + #fillAt
end

for y = 0, h - 1 do
    for x = 0, w - 1 do
        local i = y * w + x + 1
        image:drawPixel(x, y, pc.rgba(r[i], g[i], b[i], a[i]))
    end
end

local out = Sprite(image.spec)
out.cels[1].image = image
local path = app.fs.joinPath(app.fs.filePath(sprite.filename), basename .. ".png")
out:saveCopyAs(path)
out:close()

app.alert({ title = "Alpha Bleed & Export",
            text = { "Wrote " .. path,
                     w .. "x" .. h .. (wrapX and " (wrapped in X)" or ""),
                     recoloured .. " transparent pixels recoloured.",
                     "Alpha channel unchanged." } })
