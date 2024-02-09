#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""PDF Title Page Generator

Usage:
  create-title-page.py --title=<title> --subtitle=<subtitle> --image=<image_path> --footer=<footer> [--pdf=<pdf_file>]

Options:
  -h --help              Show this help message.
  --title=<title>        Title text.
  --subtitle=<subtitle>  Subtitle text.
  --image=<image_path>   Image path.
  --footer=<footer>      Footer text.
  --pdf=<pdf_file>       PDF file to prepend the title page to. [default: title-page.pdf]
"""
from PIL import Image
from docopt import docopt
from reportlab.lib.pagesizes import A4
from reportlab.lib import colors
from reportlab.pdfgen import canvas
from PyPDF2 import PdfWriter, PdfReader
import os


def create_title_page(title, subtitle, image_path, footer, pdf_file):
    c = canvas.Canvas("temp.pdf", pagesize=A4)
    page_width, page_height = A4
    c.setFillColor(colors.HexColor("#33333b"))
    c.rect(0, 0, page_width, page_height, fill=1)
    c.setFillColor(colors.HexColor("#fffef7"))

    c.setFont("Helvetica-Bold", 44)
    c.drawString(100, 650, title)
    c.setFont("Helvetica", 18)
    c.drawString(100, 620, subtitle)

    im = Image.open(image_path)
    im_width, im_height = im.size
    aspect_ratio = im_height / im_width
    new_width = page_width
    new_height = new_width * aspect_ratio
    vert_position = 0

    c.drawInlineImage(image_path, 0, vert_position, new_width, new_height)

    c.setFont("Helvetica", 12)
    c.drawString(100, 50, footer)
    c.save()

    if os.path.exists(pdf_file):
        output = PdfWriter()
        input_stream = PdfReader(open(pdf_file, "rb"))
        output.add_page(PdfReader(open("temp.pdf", "rb")).pages[0])
        for i in range(len(input_stream.pages)):
            output.add_page(input_stream.pages[i])
        with open(pdf_file, "wb") as outputStream:
            output.write(outputStream)
    else:
        os.rename("temp.pdf", pdf_file)
    os.remove("temp.pdf") if os.path.exists("temp.pdf") else None


if __name__ == "__main__":
    arguments = docopt(__doc__)
    title = arguments["--title"]
    subtitle = arguments["--subtitle"]
    image_path = arguments["--image"]
    footer = arguments["--footer"]
    pdf_file = arguments["--pdf"]
    create_title_page(title, subtitle, image_path, footer, pdf_file)
