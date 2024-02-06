#!/bin/bash
set -u

rm -rf $BUILD_DIR/html $BUILD_DIR/xml $BUILD_DIR/latex

if ! command -v doxygen &>/dev/null; then
    sudo apt-get install doxygen
fi

if ! command -v dot &>/dev/null; then
    echo "y" | sudo apt-get install graphviz
fi

if ! command -v pdflatex &>/dev/null; then
    echo "y" | sudo apt-get install texlive-latex-base
    echo "y" | sudo apt-get install texlive-latex-extra
fi

echo "DOCSDIR=$BUILD_DIR"
(cd $SOURCE_DIR && BUILD_DIR=$BUILD_DIR SOURCE_DIR=$SOURCE_DIR doxygen Doxyfile)
make -C $BUILD_DIR/latex
cp $BUILD_DIR/latex/refman.pdf $BUILD_DIR/html/docs.pdf
cp -ap $SOURCE_DIR/images $BUILD_DIR/html/images
