#pragma once
#include "Core.h"
namespace kn {
// Native PDF 1.4 writer. Glyphs are vector outlines (including Unicode fallback fonts).
// No printer, external process, rasterised page or scripting runtime is required.
class PdfCanvas {
    juce::Colour colour = juce::Colours::black;
    juce::Font font{juce::FontOptions(14)};
    static String number(float value) { return String(value, 5); }
    void path(const juce::Path &shape, bool stroke = false, float weight = 1) {
        content += number(colour.getFloatRed()) + " " + number(colour.getFloatGreen()) + " " +
                   number(colour.getFloatBlue()) + (stroke ? " RG\n" : " rg\n");
        if (stroke)
            content += number(weight) + " w\n";
        juce::Path::Iterator it(shape);
        float x = 0, y = 0;
        while (it.next())
            switch (it.elementType) {
            case juce::Path::Iterator::startNewSubPath:
                content += number(it.x1) + " " + number(it.y1) + " m\n";
                x = it.x1;
                y = it.y1;
                break;
            case juce::Path::Iterator::lineTo:
                content += number(it.x1) + " " + number(it.y1) + " l\n";
                x = it.x1;
                y = it.y1;
                break;
            case juce::Path::Iterator::quadraticTo:
                content += number(x + 2 * (it.x1 - x) / 3) + " " + number(y + 2 * (it.y1 - y) / 3) + " " +
                           number(it.x2 + 2 * (it.x1 - it.x2) / 3) + " " +
                           number(it.y2 + 2 * (it.y1 - it.y2) / 3) + " " + number(it.x2) + " " +
                           number(it.y2) + " c\n";
                x = it.x2;
                y = it.y2;
                break;
            case juce::Path::Iterator::cubicTo:
                content += number(it.x1) + " " + number(it.y1) + " " + number(it.x2) + " " + number(it.y2) +
                           " " + number(it.x3) + " " + number(it.y3) + " c\n";
                x = it.x3;
                y = it.y3;
                break;
            case juce::Path::Iterator::closePath:
                content += "h\n";
                break;
            }
        content += stroke ? "S\n" : shape.isUsingNonZeroWinding() ? "f\n" : "f*\n";
    }

  public:
    String content = "q\n1 0 0 -1 0 842 cm\n";
    void fillAll(juce::Colour c) {
        setColour(c);
        juce::Path p;
        p.addRectangle(0.f, 0.f, 595.f, 842.f);
        path(p);
    }
    void setColour(juce::Colour c) { colour = c; }
    void setFont(const juce::Font &f) { font = f; }
    void setFont(const juce::FontOptions &f) { font = juce::Font(f); }
    void line(float x1, float y1, float x2, float y2, float weight = .5f) {
        juce::Path p;
        p.startNewSubPath(x1, y1);
        p.lineTo(x2, y2);
        path(p, true, weight);
    }
    void rectangle(float x, float y, float width, float height, bool filled = false) {
        if (width <= 0 || height <= 0)
            return;
        juce::Path p;
        p.addRectangle(x, y, width, height);
        path(p, !filled, .5f);
    }
    void drawText(const String &value, int x, int y, int width, int height, juce::Justification alignment) {
        juce::GlyphArrangement glyphs;
        glyphs.addFittedText(font, value, (float)x, (float)y, (float)width, (float)height, alignment, 1, 1.f);
        juce::Path p;
        glyphs.createPath(p);
        path(p);
    }
    void diagram(juce::Rectangle<float> rect, const std::vector<int> &shape) {
        if (shape.empty())
            return;
        float width = std::max(140.f, 20.f * (shape.size() - 1) + 40), height = 180,
              scale = std::min(rect.getWidth() / width, rect.getHeight() / height);
        auto transform = juce::AffineTransform::scale(scale).translated(
            rect.getCentreX() - width * scale / 2, rect.getCentreY() - height * scale / 2);
        content += "q\n" + number(transform.mat00) + " " + number(transform.mat10) + " " +
                   number(transform.mat01) + " " + number(transform.mat11) + " " + number(transform.mat02) +
                   " " + number(transform.mat12) + " cm\n";
        float nw = 20.f * (shape.size() - 1), ns = std::min((width - 47) / nw, 157.f / 125),
              left = 12 + (width - 12) / 2 - nw * ns / 2, top = 22, dx = 20 * ns, dy = 25 * ns;
        int base = 1;
        std::vector<int> positive;
        for (int f : shape)
            if (f > 0)
                positive.push_back(f);
        if (!positive.empty() && *std::max_element(positive.begin(), positive.end()) > 5)
            base = std::max(1, *std::min_element(positive.begin(), positive.end()));
        setColour(juce::Colour(0xff254962));
        for (size_t i = 0; i < shape.size(); ++i) {
            juce::Path p;
            p.startNewSubPath(left + i * dx, top);
            p.lineTo(left + i * dx, top + 5 * dy);
            path(p, true);
        }
        for (int i = 0; i < 6; ++i) {
            juce::Path p;
            p.startNewSubPath(left, top + i * dy);
            p.lineTo(left + nw * ns, top + i * dy);
            path(p, true, i == 0 && base == 1 ? 3.f : 1.f);
        }
        setFont(juce::FontOptions("Segoe UI", 13, juce::Font::plain));
        if (base > 1)
            drawText(String(base), (int)left - 24, (int)(top + dy * .2f), 22, 18,
                     juce::Justification::centred);
        for (size_t i = 0; i < shape.size(); ++i) {
            float x = left + i * dx;
            int fret = shape[i];
            setColour(juce::Colour(0xff254962));
            if (fret < 0)
                drawText(juce::String::fromUTF8("×"), (int)x - 7, (int)top - 20, 15, 20,
                         juce::Justification::centred);
            else {
                juce::Path p;
                if (!fret) {
                    p.addEllipse(x - 4, top - 14, 8, 8);
                    path(p, true);
                } else {
                    float r = std::min(6.f, dx * .3f), y = top + (fret - base + .5f) * dy;
                    p.addEllipse(x - r, y - r, 2 * r, 2 * r);
                    setColour(juce::Colour(0xffad793c));
                    path(p);
                }
            }
        }
        content += "Q\n";
    }
};
class NativePdfDocument {
    std::vector<std::unique_ptr<PdfCanvas>> pages;

  public:
    void setPageSize(int, int) {}
    void setTitle(const String &) {}
    PdfCanvas *startPage() {
        pages.push_back(std::make_unique<PdfCanvas>());
        return pages.back().get();
    }
    bool writeToStream(juce::OutputStream &out) {
        std::vector<int64_t> offsets(3 + pages.size() * 2);
        auto write = [&](const String &s) { return out.writeText(s, false, false, nullptr); };
        auto object = [&](int id, const String &value) {
            offsets[id] = out.getPosition();
            return write(String(id) + " 0 obj\n" + value + "\nendobj\n");
        };
        if (!write("%PDF-1.4\n% KITHARA NEA native vector export\n"))
            return false;
        object(1, "<< /Type /Catalog /Pages 2 0 R >>");
        String kids;
        for (size_t i = 0; i < pages.size(); ++i)
            kids += String(3 + (int)i * 2) + " 0 R ";
        object(2, "<< /Type /Pages /Count " + String((int)pages.size()) + " /Kids [" + kids + "] >>");
        for (size_t i = 0; i < pages.size(); ++i) {
            int id = 3 + (int)i * 2;
            object(id, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << >> /Contents " +
                           String(id + 1) + " 0 R >>");
            auto commands = pages[i]->content + "Q\n";
            juce::MemoryOutputStream packed;
            {
                juce::GZIPCompressorOutputStream zip(packed, 6, 15);
                zip.write(commands.toRawUTF8(), commands.getNumBytesAsUTF8());
            }
            offsets[id + 1] = out.getPosition();
            write(String(id + 1) + " 0 obj\n<< /Length " + String((int64_t)packed.getDataSize()) +
                  " /Filter /FlateDecode >>\nstream\n");
            out.write(packed.getData(), packed.getDataSize());
            write("\nendstream\nendobj\n");
        }
        auto xref = out.getPosition();
        write("xref\n0 " + String((int)offsets.size()) + "\n0000000000 65535 f \n");
        for (size_t i = 1; i < offsets.size(); ++i)
            write(String::formatted("%010lld 00000 n \n", (long long)offsets[i]));
        write("trailer\n<< /Size " + String((int)offsets.size()) + " /Root 1 0 R >>\nstartxref\n" +
              String(xref) + "\n%%EOF\n");
        out.flush();
        if (auto *f = dynamic_cast<juce::FileOutputStream *>(&out))
            return f->getStatus().wasOk();
        return true;
    }
};
} // namespace kn
