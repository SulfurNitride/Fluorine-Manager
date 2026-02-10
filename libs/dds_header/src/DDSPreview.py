import struct
import sys
import threading
import enum

from PyQt6.QtCore import QCoreApplication, qDebug, Qt, QSize
from PyQt6.QtGui import QColor, QOpenGLContext, QSurfaceFormat, QMatrix4x4, QVector4D
from PyQt6.QtOpenGLWidgets import QOpenGLWidget
from PyQt6.QtWidgets import QGridLayout, QLabel, QPushButton, QWidget, QColorDialog, QComboBox
from PyQt6.QtOpenGL import QOpenGLBuffer, QOpenGLDebugLogger, QOpenGLShader, QOpenGLShaderProgram, QOpenGLTexture, \
    QOpenGLVersionProfile, QOpenGLVertexArrayObject, QOpenGLFunctions_4_1_Core, QOpenGLVersionFunctionsFactory

from DDS.DDSFile import DDSFile

if "mobase" not in sys.modules:
    import mobase

vertexShader2D = """
#version 150

uniform float aspectRatioRatio;

in vec4 position;
in vec2 texCoordIn;

out vec2 texCoord;

void main()
{
    texCoord = texCoordIn;
    gl_Position = position;
    if (aspectRatioRatio >= 1.0)
        gl_Position.y /= aspectRatioRatio;
    else
        gl_Position.x *= aspectRatioRatio;
}
"""

vertexShaderCube = """
#version 150

uniform float aspectRatioRatio;

in vec4 position;
in vec2 texCoordIn;

out vec2 texCoord;

void main()
{
    texCoord = texCoordIn;
    gl_Position = position;
}
"""

fragmentShaderFloat = """
#version 150

uniform sampler2D aTexture;
uniform mat4 channelMatrix;
uniform vec4 channelOffset;

in vec2 texCoord;

void main()
{
    gl_FragData[0] = channelMatrix *  texture(aTexture, texCoord) + channelOffset;
}
"""

fragmentShaderUInt = """
#version 150

uniform usampler2D aTexture;
uniform mat4 channelMatrix;
uniform vec4 channelOffset;

in vec2 texCoord;

void main()
{
    // autofilled alpha is 1, so if we have a scaling factor, we need separate ones for luminance and alpha
    gl_FragData[0] = channelMatrix * texture(aTexture, texCoord) + channelOffset;
}
"""

fragmentShaderSInt = """
#version 150

uniform isampler2D aTexture;
uniform mat4 channelMatrix;
uniform vec4 channelOffset;

in vec2 texCoord;

void main()
{
    // autofilled alpha is 1, so if we have a scaling factor and offset, we need separate ones for luminance and alpha
    gl_FragData[0] = channelMatrix * texture(aTexture, texCoord) + channelOffset;
}
"""

fragmentShaderCube = """
#version 150

uniform samplerCube aTexture;
uniform mat4 channelMatrix;
uniform vec4 channelOffset;

in vec2 texCoord;

const float PI = 3.1415926535897932384626433832795;

void main()
{
    float theta = -2.0 * PI * texCoord.x;
    float phi = PI * texCoord.y;
    gl_FragData[0] = channelMatrix * texture(aTexture, vec3(sin(theta) * sin(phi), cos(theta) * sin(phi), cos(phi))) + channelOffset;
}
"""

transparencyVS = """
#version 150

in vec4 position;

void main()
{
    gl_Position = position;
}
"""

transparencyFS = """
#version 150

uniform vec4 backgroundColour;

void main()
{
    float x = gl_FragCoord.x;
    float y = gl_FragCoord.y;
    x = mod(x, 16.0);
    y = mod(y, 16.0);
    gl_FragData[0] = x < 8.0 ^^ y < 8.0 ? vec4(vec3(191.0/255.0), 1.0) : vec4(1.0);
    gl_FragData[0].rgb = backgroundColour.rgb * backgroundColour.a + gl_FragData[0].rgb * (1.0 - backgroundColour.a);
}
"""

vertices = [
    # vertex coordinates        texture coordinates
    -1.0, -1.0, 0.5, 1.0, 0.0, 1.0,
    -1.0, 1.0, 0.5, 1.0, 0.0, 0.0,
    1.0, 1.0, 0.5, 1.0, 1.0, 0.0,

    -1.0, -1.0, 0.5, 1.0, 0.0, 1.0,
    1.0, 1.0, 0.5, 1.0, 1.0, 0.0,
    1.0, -1.0, 0.5, 1.0, 1.0, 1.0,
]


class DDSOptions:
    def __init__(self, colour: QColor = QColor(0, 0, 0, 0), channelMatrix: QMatrix4x4 = QMatrix4x4(),
                 channelOffset: QVector4D = QVector4D()):
        # QMatrix4x4 with no arguments is the identity matrix, so no channel transformations
        # declare member variables with None values
        self.backgroundColour = None
        self.channelMatrix = None
        self.channelOffset = None
        # initialize member variables with error checks
        self.setBackgroundColour(colour)
        self.setChannelMatrix(channelMatrix)
        self.setChannelOffset(channelOffset)

    def setBackgroundColour(self, colour: QColor):
        if isinstance(colour, QColor) and colour.isValid():
            self.backgroundColour = colour
        else:
            raise TypeError(str(colour) + " is not a valid QColor object.")

    def getBackgroundColour(self) -> QColor:
        return self.backgroundColour

    def getChannelMatrix(self) -> QMatrix4x4:
        return self.channelMatrix

    def setChannelMatrix(self, matrix):
        self.channelMatrix = QMatrix4x4(matrix)

    def getChannelOffset(self) -> QVector4D:
        return self.channelOffset

    def setChannelOffset(self, vector):
        self.channelOffset = QVector4D(vector)


glVersionProfile = QOpenGLVersionProfile()
glVersionProfile.setVersion(2, 1)


class DDSWidget(QOpenGLWidget):
    def __init__(self, ddsFile, ddsOptions=DDSOptions(), debugContext=False, parent=None, f=Qt.WindowType(0)):
        super(DDSWidget, self).__init__(parent, f)

        self.ddsFile = ddsFile

        self.ddsOptions = ddsOptions

        self.clean = True

        self.logger = None

        self.program = None
        self.transparecyProgram = None
        self.texture = None
        self.vbo = None
        self.vao = None

        if debugContext:
            format = QSurfaceFormat()
            format.setOption(QSurfaceFormat.FormatOption.DebugContext)
            self.setFormat(format)
            self.logger = QOpenGLDebugLogger(self)

    def __del__(self):
        self.cleanup()

    def __dtor__(self):
        self.cleanup()

    def initializeGL(self):
        if self.logger:
            self.logger.initialize()
            self.logger.messageLogged.connect(
                lambda message: qDebug(self.tr("OpenGL debug message: {0}").format(message.message())))
            self.logger.startLogging()

        gl = QOpenGLVersionFunctionsFactory.get(glVersionProfile)
        QOpenGLContext.currentContext().aboutToBeDestroyed.connect(self.cleanup)

        self.clean = False

        fragmentShader = None
        vertexShader = vertexShader2D
        if self.ddsFile.isCubemap:
            fragmentShader = fragmentShaderCube
            vertexShader = vertexShaderCube
            if QOpenGLContext.currentContext().hasExtension(b"GL_ARB_seamless_cube_map"):
                GL_TEXTURE_CUBE_MAP_SEAMLESS = 0x884F
                gl.glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS)
        elif self.ddsFile.glFormat.samplerType == "F":
            fragmentShader = fragmentShaderFloat
        elif self.ddsFile.glFormat.samplerType == "UI":
            fragmentShader = fragmentShaderUInt
        else:
            fragmentShader = fragmentShaderSInt

        self.program = QOpenGLShaderProgram(self)
        self.program.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Vertex, vertexShader)
        self.program.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Fragment, fragmentShader)
        self.program.bindAttributeLocation("position", 0)
        self.program.bindAttributeLocation("texCoordIn", 1)
        self.program.link()

        self.transparecyProgram = QOpenGLShaderProgram(self)
        self.transparecyProgram.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Vertex, transparencyVS)
        self.transparecyProgram.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Fragment, transparencyFS)
        self.transparecyProgram.bindAttributeLocation("position", 0)
        self.transparecyProgram.link()

        self.vao = QOpenGLVertexArrayObject(self)
        vaoBinder = QOpenGLVertexArrayObject.Binder(self.vao)

        self.vbo = QOpenGLBuffer(QOpenGLBuffer.Type.VertexBuffer)
        self.vbo.create()
        self.vbo.bind()

        theBytes = struct.pack("%sf" % len(vertices), *vertices)
        self.vbo.allocate(theBytes, len(theBytes))

        gl.glEnableVertexAttribArray(0)
        gl.glEnableVertexAttribArray(1)
        gl.glVertexAttribPointer(0, 4, gl.GL_FLOAT, False, 6 * 4, 0)
        gl.glVertexAttribPointer(1, 2, gl.GL_FLOAT, False, 6 * 4, 4 * 4)

        self.texture = self.ddsFile.asQOpenGLTexture(gl, QOpenGLContext.currentContext())

    def resizeGL(self, w, h):
        aspectRatioTex = self.texture.width() / self.texture.height() if self.texture else 1.0
        aspectRatioWidget = w / h
        ratioRatio = aspectRatioTex / aspectRatioWidget

        self.program.bind()
        self.program.setUniformValue("aspectRatioRatio", ratioRatio)
        self.program.release()

    def paintGL(self):
        gl = QOpenGLVersionFunctionsFactory.get(glVersionProfile)

        vaoBinder = QOpenGLVertexArrayObject.Binder(self.vao)

        # Draw checkerboard so transparency is obvious
        self.transparecyProgram.bind()

        backgroundColour = self.ddsOptions.getBackgroundColour()
        if backgroundColour and backgroundColour.isValid():
            self.transparecyProgram.setUniformValue("backgroundColour", backgroundColour)

        gl.glDrawArrays(gl.GL_TRIANGLES, 0, 6)

        self.transparecyProgram.release()

        self.program.bind()

        if self.texture:
            self.texture.bind()

        gl.glEnable(gl.GL_BLEND)
        gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE_MINUS_SRC_ALPHA)

        self.program.setUniformValue("channelMatrix", self.ddsOptions.getChannelMatrix())
        self.program.setUniformValue("channelOffset", self.ddsOptions.getChannelOffset())

        gl.glDrawArrays(gl.GL_TRIANGLES, 0, 6)

        if self.texture:
            self.texture.release()
        self.program.release()

    def cleanup(self):
        if not self.clean:
            self.makeCurrent()

            self.program = None
            self.transparecyProgram = None
            if self.texture:
                self.texture.destroy()
            self.texture = None
            self.vbo.destroy()
            self.vbo = None
            self.vao.destroy()
            self.vao = None

            self.doneCurrent()
            self.clean = True

    def tr(self, str):
        return QCoreApplication.translate("DDSWidget", str)


class ColourChannels(enum.Enum):
    RGBA = "Colour and Alpha"
    RGB = "Colour"
    A = "Alpha"
    R = "Red"
    G = "Green"
    B = "Blue"


class DDSChannelManager:
    def __init__(self, channels: ColourChannels):
        self.channels = channels

    def setChannels(self, options: DDSOptions, channels: ColourChannels):
        self.channels = channels

        def drawColour(alpha: bool):
            colorMatrix = QMatrix4x4()
            colorOffset = QVector4D()
            if not alpha:
                colorMatrix[3, 3] = 0
                colorOffset.setW(1.0)
            options.setChannelMatrix(colorMatrix)
            options.setChannelOffset(colorOffset)

        def drawGrayscale(channel: ColourChannels):
            colorOffset = QVector4D(0, 0, 0, 1)
            channelVector = [0, 0, 0, 0]
            if channels == ColourChannels.R:
                channelVector[0] = 1
            elif channel == ColourChannels.G:
                channelVector[1] = 1
            elif channel == ColourChannels.B:
                channelVector[2] = 1
            elif channels == ColourChannels.A:
                channelVector[3] = 1
            else:
                raise ValueError("channel must be a single color channel.")
            alphaVector = [0, 0, 0, 0]
            colorMatrix = channelVector * 3 + alphaVector
            options.setChannelMatrix(colorMatrix)
            options.setChannelOffset(colorOffset)

        if channels == ColourChannels.RGBA:
            drawColour(True)
        elif channels == ColourChannels.RGB:
            drawColour(False)
        else:
            drawGrayscale(channels)


class DDSPreview(mobase.IPluginPreview):

    def __init__(self):
        super().__init__()
        self.__organizer = None
        self.options = None
        self.channelManager = None

    def init(self, organizer: mobase.IOrganizer):
        self.__organizer = organizer
        savedColour = QColor(self.pluginSetting("background r"), self.pluginSetting("background g"),
                             self.pluginSetting("background b"), self.pluginSetting("background a"))
        try:
            savedChannels = ColourChannels[self.pluginSetting("channels")]
        except KeyError:
            savedChannels = ColourChannels.RGBA
        self.options = DDSOptions(savedColour)
        self.channelManager = DDSChannelManager(savedChannels)
        self.channelManager.setChannels(self.options, savedChannels)
        return True

    def pluginSetting(self, name):
        return self.__organizer.pluginSetting(self.name(), name)

    def setPluginSetting(self, name, value):
        self.__organizer.setPluginSetting(self.name(), name, value)

    def name(self):
        return "DDS Preview Plugin"

    def author(self):
        return "AnyOldName3"

    def description(self):
        return self.tr("Lets you preview DDS files by actually uploading them to the GPU.")

    def version(self):
        return mobase.VersionInfo(1, 0, 1, 0)

    def settings(self):
        return [mobase.PluginSetting("log gl errors", self.tr(
            "If enabled, log OpenGL errors and debug messages. May decrease performance."), False),
                mobase.PluginSetting("background r", self.tr("Red channel of background colour"), 0),
                mobase.PluginSetting("background g", self.tr("Green channel of background colour"), 0),
                mobase.PluginSetting("background b", self.tr("Blue channel of background colour"), 0),
                mobase.PluginSetting("background a", self.tr("Alpha channel of background colour"), 0),
                mobase.PluginSetting("channels", self.tr("The colour channels that are displayed."),
                                     ColourChannels.RGBA.name)]

    def supportedExtensions(self) -> set[str]:
        return {"dds"}

    def supportsArchives(self) -> bool:
        return True

    def genFilePreview(self, fileName: str, maxSize: QSize) -> QWidget:
        return self.previewFromDDSFile(DDSFile.fromFile(fileName))

    def genDataPreview(self, fileData: bytes, fileName: str, maxSize: QSize) -> QWidget:
        return self.previewFromDDSFile(DDSFile(fileData, fileName))

    def previewFromDDSFile(self, ddsFile: DDSFile) -> QWidget:
        ddsFile.load()
        layout = QGridLayout()
        # Image grows before label and button
        layout.setRowStretch(0, 1)
        # Label grows before button
        layout.setColumnStretch(0, 1)
        layout.addWidget(self.__makeLabel(ddsFile), 1, 0, 1, 1)

        ddsWidget = DDSWidget(ddsFile, self.options, self.__organizer.pluginSetting(self.name(), "log gl errors"))
        layout.addWidget(ddsWidget, 0, 0, 1, 3)

        layout.addWidget(self.__makeColourButton(ddsWidget), 1, 2, 1, 1)
        layout.addWidget(self.__makeChannelsButton(ddsWidget), 1, 1, 1, 1)

        widget = QWidget()
        widget.setLayout(layout)
        return widget

    def tr(self, str):
        return QCoreApplication.translate("DDSPreview", str)

    def __makeLabel(self, ddsFile):
        label = QLabel(ddsFile.getDescription())
        label.setWordWrap(True)
        label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        return label

    def __makeColourButton(self, ddsWidget):
        button = QPushButton(self.tr("Pick background colour"))

        def pickColour(unused):
            newColour = QColorDialog.getColor(self.options.getBackgroundColour(), button, "Background colour",
                                              QColorDialog.ColorDialogOption.ShowAlphaChannel)
            if newColour.isValid():
                self.setPluginSetting("background r", newColour.red())
                self.setPluginSetting("background g", newColour.green())
                self.setPluginSetting("background b", newColour.blue())
                self.setPluginSetting("background a", newColour.alpha())
                self.options.setBackgroundColour(newColour)
                ddsWidget.update()

        button.clicked.connect(pickColour)
        return button

    def __makeChannelsButton(self, ddsWidget):
        listwidget = QComboBox()
        channelKeys = [e.name for e in ColourChannels]
        channelNames = [e.value for e in ColourChannels]

        listwidget.addItems(channelNames)
        listwidget.setCurrentText(self.channelManager.channels.value)
        listwidget.setToolTip(self.tr("Select what colour channels are displayed."))

        listwidget.showEvent = lambda _: listwidget.setCurrentText(self.channelManager.channels.value)

        def onChanged(newIndex):
            self.channelManager.setChannels(self.options, ColourChannels[channelKeys[newIndex]])
            self.setPluginSetting("channels", self.channelManager.channels.name)
            ddsWidget.update()

        listwidget.currentIndexChanged.connect(onChanged)
        return listwidget


def createPlugin():
    return DDSPreview()
