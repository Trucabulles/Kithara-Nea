"""Build the two bundled manuals; development tool, not shipped in the application."""
from pathlib import Path
import shutil
from xml.sax.saxutils import escape
from reportlab.platypus import SimpleDocTemplate, Paragraph
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib import colors
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from pypdf import PdfReader

root = Path(__file__).resolve().parents[1]
pdfmetrics.registerFont(TTFont('Segoe', 'C:/Windows/Fonts/segoeui.ttf'))
pdfmetrics.registerFont(TTFont('SegoeBold', 'C:/Windows/Fonts/segoeuib.ttf'))
styles = {
 'body': ParagraphStyle('body',fontName='Segoe',fontSize=10.5,leading=15,textColor=colors.HexColor('#254962'),spaceAfter=10),
 'title': ParagraphStyle('title',fontName='SegoeBold',fontSize=28,leading=34,textColor=colors.HexColor('#254962'),spaceAfter=16),
 'heading': ParagraphStyle('heading',fontName='SegoeBold',fontSize=15,leading=21,textColor=colors.HexColor('#94642f'),spaceBefore=12,spaceAfter=10,keepWithNext=True)
}
for code, source, target in [('FR','MANUEL-FR.md','KITHARA-NEA-Manuel-FR.pdf'),('EN','MANUAL-EN.md','KITHARA-NEA-Manual-EN.pdf')]:
 path=root/'output/pdf'/target
 def frame(canvas, doc):
  w,h=doc.pagesize
  canvas.setFillColor(colors.HexColor('#f9faf4'));canvas.rect(0,0,w,h,fill=1,stroke=0)
  canvas.setStrokeColor(colors.HexColor('#cbbda5'));canvas.line(44,40,w-44,40)
  canvas.setFont('Segoe',9);canvas.setFillColor(colors.HexColor('#254962'))
  canvas.drawString(44,25,'KITHÁRA NÉA · '+('Manuel' if code=='FR' else 'User manual'));canvas.drawRightString(w-44,25,str(doc.page))
 story=[]
 for block in (root/'documentation'/source).read_text(encoding='utf-8-sig').strip().split('\n\n'):
  block=block.strip()
  if not block:continue
  if block.startswith('# '):
   for line in block.splitlines():
    style='title' if line.startswith('# ') else 'heading' if line.startswith('## ') else 'body'
    story.append(Paragraph(escape(line.lstrip('# ')),styles[style]))
  elif block.startswith('## '):
   heading, _, body = block.partition('\n')
   story.append(Paragraph(escape(heading[3:]),styles['heading']))
   if body:story.append(Paragraph(escape(body).replace('\n','<br/>'),styles['body']))
  else:story.append(Paragraph(escape(block).replace('\n','<br/>'),styles['body']))
 doc=SimpleDocTemplate(str(path),pagesize=(595.28,841.89),leftMargin=44,rightMargin=44,topMargin=38,bottomMargin=56,title='KITHÁRA NÉA — '+code,author='Trucabulles')
 doc.build(story,onFirstPage=frame,onLaterPages=frame)
 shutil.copy2(path,root/'resources/manuals'/target)
 pages=PdfReader(path).pages
 assert all(p.extract_text().strip() for p in pages)
 print(target,len(pages),'pages')
