///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2011, Industrial Light & Magic, a division of Lucas
// Digital Ltd. LLC
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Industrial Light & Magic nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////

#include "testCopyDeepTiled.h"

#include <tmpDir.h>

#include <assert.h>
#include <string.h>

#include <ImfDeepTiledInputFile.h>
#include <ImfDeepTiledOutputFile.h>
#include <ImfChannelList.h>
#include <ImfArray.h>
#include <ImfPartType.h>
#include <IlmThreadPool.h>

#include <stdio.h>
#include <stdlib.h>
#include <vector>

using namespace OPENEXR_IMF_NAMESPACE;
using namespace IMATH_NAMESPACE;
using namespace ILMTHREAD_NAMESPACE;
using namespace std;

namespace
{

const int width = 199;
const int height = 369;
const int minX = 23;
const int minY = 59;
const Box2i dataWindow(V2i(minX, minY), V2i(minX + width - 1, minY + height - 1));
const Box2i displayWindow(V2i(0, 0), V2i(minX + width * 2, minY + height * 2));
const char source_filename[] = IMF_TMP_DIR "imf_test_copy_deep_tiled_source.exr";
const char copy_filename[] = IMF_TMP_DIR "imf_test_copy_deep_tiled_copy.exr";

vector<int> channelTypes;
Array2D< Array2D<unsigned int> > sampleCountWhole;
Header header;

void generateRandomFile(int channelCount, Compression compression)
{

    cout << "generating " << flush;
    header = Header(displayWindow, dataWindow,
                    1,
                    IMATH_NAMESPACE::V2f (0, 0),
                    1,
                    INCREASING_Y,
                    compression);
    cout << "compression " << compression << " " << flush;

    //
    // Add channels.
    //

    channelTypes.clear();

    for (int i = 0; i < channelCount; i++)
    {
        int type = rand() % 3;
        stringstream ss;
        ss << i;
        string str = ss.str();
        if (type == 0)
            header.channels().insert(str, Channel(OPENEXR_IMF_NAMESPACE::UINT));
        if (type == 1)
            header.channels().insert(str, Channel(OPENEXR_IMF_NAMESPACE::HALF));
        if (type == 2)
            header.channels().insert(str, Channel(OPENEXR_IMF_NAMESPACE::FLOAT));
        channelTypes.push_back(type);
    }

    header.setType(DEEPTILE);
    header.setTileDescription(
        TileDescription(rand() % width + 1, rand() % height + 1, RIPMAP_LEVELS));

    Array<Array2D< void* > > data(channelCount);
    for (int i = 0; i < channelCount; i++)
        data[i].resizeErase(height, width);

    Array2D<unsigned int> sampleCount;
    sampleCount.resizeErase(height, width);

    remove (source_filename);
    DeepTiledOutputFile file(source_filename, header, 8);

    cout << "tileSizeX " << file.tileXSize() << " tileSizeY " << file.tileYSize() << " ";

    sampleCountWhole.resizeErase(file.numYLevels(), file.numXLevels());
    for (int i = 0; i < sampleCountWhole.height(); i++)
        for (int j = 0; j < sampleCountWhole.width(); j++)
            sampleCountWhole[i][j].resizeErase(height, width);

    DeepFrameBuffer frameBuffer;

    int memOffset = dataWindow.min.x + dataWindow.min.y * width;

    frameBuffer.insertSampleCountSlice (Slice (OPENEXR_IMF_NAMESPACE::UINT,
                                        (char *) (&sampleCount[0][0] - memOffset),
                                        sizeof (unsigned int) * 1,
                                        sizeof (unsigned int) * width,
                                        1, 1,
                                        0,
                                        false,
                                        false));

    for (int i = 0; i < channelCount; i++)
    {
        PixelType type;
        if (channelTypes[i] == 0)
            type = OPENEXR_IMF_NAMESPACE::UINT;
        if (channelTypes[i] == 1)
            type = OPENEXR_IMF_NAMESPACE::HALF;
        if (channelTypes[i] == 2)
            type = OPENEXR_IMF_NAMESPACE::FLOAT;

        stringstream ss;
        ss << i;
        string str = ss.str();

        int sampleSize;
        if (channelTypes[i] == 0) sampleSize = sizeof (unsigned int);
        if (channelTypes[i] == 1) sampleSize = sizeof (half);
        if (channelTypes[i] == 2) sampleSize = sizeof (float);

        int pointerSize = sizeof (char *);

        frameBuffer.insert (str,
                            DeepSlice (type,
                            (char *) (&data[i][0][0] - memOffset),
                            pointerSize * 1,
                            pointerSize * width,
                            sampleSize,
                            1, 1,
                            0,
                            false,
                            false));
    }

    file.setFrameBuffer(frameBuffer);

    cout << "writing " << flush;

    for (int ly = 0; ly < file.numYLevels(); ly++)
        for (int lx = 0; lx < file.numXLevels(); lx++)
        {
            Box2i dataWindowL = file.dataWindowForLevel(lx, ly);

                //
                // Bulk write (without relative coordinates).
                //

                for (int j = 0; j < file.numYTiles(ly); j++)
                {
                    for (int i = 0; i < file.numXTiles(lx); i++)
                    {
                        Box2i box = file.dataWindowForTile(i, j, lx, ly);
                        for (int y = box.min.y; y <= box.max.y; y++)
                            for (int x = box.min.x; x <= box.max.x; x++)
                            {
                                int dwy = y - dataWindowL.min.y;
                                int dwx = x - dataWindowL.min.x;
                                sampleCount[dwy][dwx] = rand() % 10 + 1;
                                sampleCountWhole[ly][lx][dwy][dwx] = sampleCount[dwy][dwx];
                                for (int k = 0; k < channelCount; k++)
                                {
                                    if (channelTypes[k] == 0)
                                        data[k][dwy][dwx] = new unsigned int[sampleCount[dwy][dwx]];
                                    if (channelTypes[k] == 1)
                                        data[k][dwy][dwx] = new half[sampleCount[dwy][dwx]];
                                    if (channelTypes[k] == 2)
                                        data[k][dwy][dwx] = new float[sampleCount[dwy][dwx]];
                                    for (int l = 0; l < sampleCount[dwy][dwx]; l++)
                                    {
                                        if (channelTypes[k] == 0)
                                            ((unsigned int*)data[k][dwy][dwx])[l] = (dwy * width + dwx) % 2049;
                                        if (channelTypes[k] == 1)
                                            ((half*)data[k][dwy][dwx])[l] = (dwy* width + dwx) % 2049;
                                        if (channelTypes[k] == 2)
                                            ((float*)data[k][dwy][dwx])[l] = (dwy * width + dwx) % 2049;
                                    }
                                }
                            }
                    }
                }
        
             file.writeTiles(0, file.numXTiles(lx) - 1, 0, file.numYTiles(ly) - 1, lx, ly);
        
        
        for (int i = 0; i < file.levelHeight(ly); i++)
            for (int j = 0; j < file.levelWidth(lx); j++)
                 for (int k = 0; k < channelCount; k++)
                 {
                      if (channelTypes[k] == 0)
                             delete[] (unsigned int*) data[k][i][j];
                      if (channelTypes[k] == 1)
                             delete[] (half*) data[k][i][j];
                      if (channelTypes[k] == 2)
                             delete[] (float*) data[k][i][j];
                 }
        }    
}

void checkValue(void* sampleRawData, int sampleCount, int channelType, int dwx, int dwy)
{
    for (int l = 0; l < sampleCount; l++)
    {
        if (channelType == 0)
        {
            unsigned int* value = (unsigned int*)(sampleRawData);
            if (value[l] != (dwy * width + dwx) % 2049)
                cout << dwx << ", " << dwy << " error, should be "
                     << (dwy * width + dwx) % 2049 << ", is " << value[l]
                     << endl << flush;
            assert (value[l] == (dwy * width + dwx) % 2049);
        }
        if (channelType == 1)
        {
            half* value = (half*)(sampleRawData);
            if (value[l] != (dwy * width + dwx) % 2049)
                cout << dwx << ", " << dwy << " error, should be "
                     << (dwy * width + dwx) % 2049 << ", is " << value[l]
                     << endl << flush;
            assert (value[l] == (dwy * width + dwx) % 2049);
        }
        if (channelType == 2)
        {
            float* value = (float*)(sampleRawData);
            if (value[l] != (dwy * width + dwx) % 2049)
                cout << dwx << ", " << dwy << " error, should be "
                     << (dwy * width + dwx) % 2049 << ", is " << value[l]
                     << endl << flush;
            assert (value[l] == (dwy * width + dwx) % 2049);
        }
    }
}

void readFile(int channelCount)
{

    cout << "reading " << flush;

    DeepTiledInputFile file(copy_filename, 8);

    const Header& fileHeader = file.header();
    assert (fileHeader.displayWindow() == header.displayWindow());
    assert (fileHeader.dataWindow() == header.dataWindow());
    assert (fileHeader.pixelAspectRatio() == header.pixelAspectRatio());
    assert (fileHeader.screenWindowCenter() == header.screenWindowCenter());
    assert (fileHeader.screenWindowWidth() == header.screenWindowWidth());
    assert (fileHeader.lineOrder() == header.lineOrder());
    assert (fileHeader.compression() == header.compression());
    assert (fileHeader.channels() == header.channels());
    assert (fileHeader.type() == header.type());
    assert (fileHeader.tileDescription() == header.tileDescription());

    Array2D<unsigned int> localSampleCount;
    localSampleCount.resizeErase(height, width);
    Array<Array2D< void* > > data(channelCount);
    for (int i = 0; i < channelCount; i++)
        data[i].resizeErase(height, width);

    DeepFrameBuffer frameBuffer;

    int memOffset = dataWindow.min.x + dataWindow.min.y * width;
    frameBuffer.insertSampleCountSlice (Slice (OPENEXR_IMF_NAMESPACE::UINT,
                                        (char *) (&localSampleCount[0][0] - memOffset),
                                        sizeof (unsigned int) * 1,
                                        sizeof (unsigned int) * width,
                                        1, 1,
                                        0,
                                        false,
                                        false));

    for (int i = 0; i < channelCount; i++)
    {
        PixelType type;
        if (channelTypes[i] == 0)
            type = OPENEXR_IMF_NAMESPACE::UINT;
        if (channelTypes[i] == 1)
            type = OPENEXR_IMF_NAMESPACE::HALF;
        if (channelTypes[i] == 2)
            type = OPENEXR_IMF_NAMESPACE::FLOAT;

        stringstream ss;
        ss << i;
        string str = ss.str();

        int sampleSize;
        if (channelTypes[i] == 0) sampleSize = sizeof (unsigned int);
        if (channelTypes[i] == 1) sampleSize = sizeof (half);
        if (channelTypes[i] == 2) sampleSize = sizeof (float);

        int pointerSize = sizeof (char *);

        frameBuffer.insert (str,
                            DeepSlice (type,
                            (char *) (&data[i][0][0] - memOffset),
                            pointerSize * 1,
                            pointerSize * width,
                            sampleSize,
                            1, 1,
                            0,
                            false,
                            false));
    }

    file.setFrameBuffer(frameBuffer);


    for (int ly = 0; ly < file.numYLevels(); ly++)
        for (int lx = 0; lx < file.numXLevels(); lx++)
        {
            Box2i dataWindowL = file.dataWindowForLevel(lx, ly);

                //
                // Testing bulk read (without relative coordinates).
                //

                file.readPixelSampleCounts(0, file.numXTiles(lx) - 1, 0, file.numYTiles(ly) - 1, lx, ly);

                for (int i = 0; i < file.numYTiles(ly); i++)
                {
                    for (int j = 0; j < file.numXTiles(lx); j++)
                    {
                        Box2i box = file.dataWindowForTile(j, i, lx, ly);
                        for (int y = box.min.y; y <= box.max.y; y++)
                            for (int x = box.min.x; x <= box.max.x; x++)
                            {
                                int dwy = y - dataWindowL.min.y;
                                int dwx = x - dataWindowL.min.x;
                                assert(localSampleCount[dwy][dwx] == sampleCountWhole[ly][lx][dwy][dwx]);

                                for (int k = 0; k < channelTypes.size(); k++)
                                {
                                    if (channelTypes[k] == 0)
                                        data[k][dwy][dwx] = new unsigned int[localSampleCount[dwy][dwx]];
                                    if (channelTypes[k] == 1)
                                        data[k][dwy][dwx] = new half[localSampleCount[dwy][dwx]];
                                    if (channelTypes[k] == 2)
                                        data[k][dwy][dwx] = new float[localSampleCount[dwy][dwx]];
                                }
                            }
                    }
                }

                file.readTiles(0, file.numXTiles(lx) - 1, 0, file.numYTiles(ly) - 1, lx, ly);
            

                for (int i = 0; i < file.levelHeight(ly); i++)
                    for (int j = 0; j < file.levelWidth(lx); j++)
                        for (int k = 0; k < channelCount; k++)
                        {
                            for (int l = 0; l < localSampleCount[i][j]; l++)
                            {
                                if (channelTypes[k] == 0)
                                {
                                    unsigned int* value = (unsigned int*)(data[k][i][j]);
                                    if (value[l] != (i * width + j) % 2049)
                                        cout << j << ", " << i << " error, should be "
                                             << (i * width + j) % 2049 << ", is " << value[l]
                                             << endl << flush;
                                    assert (value[l] == (i * width + j) % 2049);
                                }
                                if (channelTypes[k] == 1)
                                {
                                    half* value = (half*)(data[k][i][j]);
                                    if (value[l] != (i * width + j) % 2049)
                                        cout << j << ", " << i << " error, should be "
                                             << (i * width + j) % 2049 << ", is " << value[l]
                                             << endl << flush;
                                    assert (((half*)(data[k][i][j]))[l] == (i * width + j) % 2049);
                                }
                                if (channelTypes[k] == 2)
                                {
                                    float* value = (float*)(data[k][i][j]);
                                    if (value[l] != (i * width + j) % 2049)
                                        cout << j << ", " << i << " error, should be "
                                             << (i * width + j) % 2049 << ", is " << value[l]
                                             << endl << flush;
                                    assert (((float*)(data[k][i][j]))[l] == (i * width + j) % 2049);
                                }
                            }
                        }


                for (int i = 0; i < file.levelHeight(ly); i++)
                    for (int j = 0; j < file.levelWidth(lx); j++)
                        for (int k = 0; k < channelCount; k++)
                        {
                            if (channelTypes[k] == 0)
                                delete[] (unsigned int*) data[k][i][j];
                            if (channelTypes[k] == 1)
                                delete[] (half*) data[k][i][j];
                            if (channelTypes[k] == 2)
                                delete[] (float*) data[k][i][j];
                        }
        }
}

void copyFile()
{
    cout << "copying " ;
    cout.flush();
    {
        DeepTiledInputFile in_file(source_filename,8);
        remove (copy_filename);
        DeepTiledOutputFile out_file(copy_filename,in_file.header(),8);
        out_file.copyPixels(in_file);
    }
    // prevent accidentally reading souce instead of copy
    remove(source_filename);
}

void readCopyWriteTest(int channelCount, int testTimes)
{
    cout << "Testing files with " << channelCount << " channels, using absolute coordinates "
         << testTimes << " times."
         << endl << flush;
         
    for (int i = 0; i < testTimes; i++)
    {
        int compressionIndex = i % 3;
        Compression compression;
        switch (compressionIndex)
        {
            case 0:
                compression = NO_COMPRESSION;
                break;
            case 1:
                compression = RLE_COMPRESSION;
                break;
            case 2:
                compression = ZIPS_COMPRESSION;
                break;
        }

        generateRandomFile(channelCount, compression);
        copyFile();
        readFile(channelCount);
        remove (copy_filename);
        cout << endl << flush;

    }
}

} // namespace

void testCopyDeepTiled()
{
    try
    {
        cout << "Testing raw copy in DeepTiledInput/OutputFile " << endl;

        srand(1);

        int numThreads = ThreadPool::globalThreadPool().numThreads();
        ThreadPool::globalThreadPool().setNumThreads(2);

        readCopyWriteTest(3, 25);
        readCopyWriteTest(10, 12);
        readCopyWriteTest(22, 13);
        
        ThreadPool::globalThreadPool().setNumThreads(numThreads);

        cout << "ok\n" << endl;
    }
    catch (const std::exception &e)
    {
        cerr << "ERROR -- caught exception: " << e.what() << endl;
        assert (false);
    }
}
