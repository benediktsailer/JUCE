/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-11 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

BEGIN_JUCE_NAMESPACE


//==============================================================================
OpenGLPixelFormat::OpenGLPixelFormat (const int bitsPerRGBComponent,
                                      const int alphaBits_,
                                      const int depthBufferBits_,
                                      const int stencilBufferBits_) noexcept
    : redBits (bitsPerRGBComponent),
      greenBits (bitsPerRGBComponent),
      blueBits (bitsPerRGBComponent),
      alphaBits (alphaBits_),
      depthBufferBits (depthBufferBits_),
      stencilBufferBits (stencilBufferBits_),
      accumulationBufferRedBits (0),
      accumulationBufferGreenBits (0),
      accumulationBufferBlueBits (0),
      accumulationBufferAlphaBits (0),
      multisamplingLevel (0)
{
}

OpenGLPixelFormat::OpenGLPixelFormat (const OpenGLPixelFormat& other) noexcept
    : redBits (other.redBits),
      greenBits (other.greenBits),
      blueBits (other.blueBits),
      alphaBits (other.alphaBits),
      depthBufferBits (other.depthBufferBits),
      stencilBufferBits (other.stencilBufferBits),
      accumulationBufferRedBits (other.accumulationBufferRedBits),
      accumulationBufferGreenBits (other.accumulationBufferGreenBits),
      accumulationBufferBlueBits (other.accumulationBufferBlueBits),
      accumulationBufferAlphaBits (other.accumulationBufferAlphaBits),
      multisamplingLevel (other.multisamplingLevel)
{
}

OpenGLPixelFormat& OpenGLPixelFormat::operator= (const OpenGLPixelFormat& other) noexcept
{
    redBits = other.redBits;
    greenBits = other.greenBits;
    blueBits = other.blueBits;
    alphaBits = other.alphaBits;
    depthBufferBits = other.depthBufferBits;
    stencilBufferBits = other.stencilBufferBits;
    accumulationBufferRedBits = other.accumulationBufferRedBits;
    accumulationBufferGreenBits = other.accumulationBufferGreenBits;
    accumulationBufferBlueBits = other.accumulationBufferBlueBits;
    accumulationBufferAlphaBits = other.accumulationBufferAlphaBits;
    multisamplingLevel = other.multisamplingLevel;
    return *this;
}

bool OpenGLPixelFormat::operator== (const OpenGLPixelFormat& other) const noexcept
{
    return redBits == other.redBits
            && greenBits == other.greenBits
            && blueBits == other.blueBits
            && alphaBits == other.alphaBits
            && depthBufferBits == other.depthBufferBits
            && stencilBufferBits == other.stencilBufferBits
            && accumulationBufferRedBits == other.accumulationBufferRedBits
            && accumulationBufferGreenBits == other.accumulationBufferGreenBits
            && accumulationBufferBlueBits == other.accumulationBufferBlueBits
            && accumulationBufferAlphaBits == other.accumulationBufferAlphaBits
            && multisamplingLevel == other.multisamplingLevel;
}

//==============================================================================
static Array<OpenGLContext*> knownContexts;

OpenGLContext::OpenGLContext() noexcept
{
    knownContexts.add (this);
}

OpenGLContext::~OpenGLContext()
{
    knownContexts.removeValue (this);
}

OpenGLContext* OpenGLContext::getCurrentContext()
{
    for (int i = knownContexts.size(); --i >= 0;)
    {
        OpenGLContext* const oglc = knownContexts.getUnchecked(i);

        if (oglc->isActive())
            return oglc;
    }

    return nullptr;
}

//==============================================================================
class OpenGLComponent::OpenGLComponentWatcher  : public ComponentMovementWatcher
{
public:
    OpenGLComponentWatcher (OpenGLComponent& owner_)
        : ComponentMovementWatcher (&owner_),
          owner (owner_)
    {
    }

    void componentMovedOrResized (bool /*wasMoved*/, bool /*wasResized*/)
    {
        owner.updateContextPosition();
    }

    void componentPeerChanged()
    {
        owner.recreateContextAsync();
    }

    void componentVisibilityChanged()
    {
        if (! owner.isShowing())
            owner.stopBackgroundThread();
    }

private:
    OpenGLComponent& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenGLComponentWatcher);
};

//==============================================================================
class OpenGLComponent::OpenGLComponentRenderThread  : public Thread
{
public:
    OpenGLComponentRenderThread (OpenGLComponent& owner_)
        : Thread ("OpenGL Render"),
          owner (owner_)
    {
    }

    void run()
    {
       #if JUCE_LINUX
        {
            MessageManagerLock mml (this);

            if (! mml.lockWasGained())
                return;

            owner.updateContext();
            owner.updateContextPosition();
        }
       #endif

        while (! threadShouldExit())
        {
            const uint32 startOfRendering = Time::getMillisecondCounter();

            if (! owner.renderAndSwapBuffers())
                break;

            const int elapsed = (int) (Time::getMillisecondCounter() - startOfRendering);
            Thread::sleep (jmax (1, 20 - elapsed));
        }

       #if JUCE_LINUX
        owner.deleteContext();
       #endif
    }

private:
    OpenGLComponent& owner;

    JUCE_DECLARE_NON_COPYABLE (OpenGLComponentRenderThread);
};

void OpenGLComponent::startRenderThread()
{
    if (renderThread == nullptr)
        renderThread = new OpenGLComponentRenderThread (*this);

    renderThread->startThread (6);
}

void OpenGLComponent::stopRenderThread()
{
    if (renderThread != nullptr)
    {
        renderThread->stopThread (5000);
        renderThread = nullptr;
    }

   #if ! JUCE_LINUX
    deleteContext();
   #endif
}

//==============================================================================
class OpenGLComponent::OpenGLCachedComponentImage  : public CachedComponentImage,
                                                     public Timer // N.B. using a Timer rather than an AsyncUpdater
                                                                  // to avoid scheduling problems on Windows
{
public:
    OpenGLCachedComponentImage (OpenGLComponent& owner_)
        : owner (owner_)
    {
    }

    void paint (Graphics&)
    {
        ComponentPeer* const peer = owner.getPeer();

        if (peer != nullptr)
        {
            const Point<int> topLeft (owner.getScreenPosition() - peer->getScreenPosition());
            peer->addMaskedRegion (topLeft.x, topLeft.y, owner.getWidth(), owner.getHeight());
        }

        if (owner.isUsingDedicatedThread())
        {
            if (peer != nullptr && owner.isShowing())
            {
               #if ! JUCE_LINUX
                owner.updateContext();
               #endif

                if (! owner.threadStarted)
                {
                    owner.threadStarted = true;
                    owner.startRenderThread();
                }
            }
        }
        else
        {
            owner.updateContext();

            if (isTimerRunning())
                timerCallback();
        }
    }

    void invalidateAll()
    {
        validArea.clear();
        triggerRepaint();
    }

    void invalidate (const Rectangle<int>& area)
    {
        validArea.subtract (area);
        triggerRepaint();
    }

    void releaseResources()
    {
        frameBuffer.release();
    }

    void timerCallback()
    {
        stopTimer();

        if (! owner.makeCurrentRenderingTarget())
            return;

        const Rectangle<int> bounds (owner.getLocalBounds());

        const int fbW = frameBuffer.getWidth();
        const int fbH = frameBuffer.getHeight();

        if (fbW < bounds.getWidth()
             || fbH < bounds.getHeight()
             || fbW > bounds.getWidth() + 128
             || fbH > bounds.getHeight() + 128
             || ! frameBuffer.isValid())
        {
            frameBuffer.initialise (bounds.getWidth(), bounds.getHeight());
            validArea.clear();
        }

        owner.renderOpenGL();

        if ((owner.flags & allowSubComponents) != 0)
        {
            {
                RectangleList invalid (bounds);
                invalid.subtract (validArea);
                validArea = bounds;

                if (! invalid.isEmpty())
                {
                    OpenGLRenderer g (frameBuffer);
                    g.clipToRectangleList (invalid);

                    g.setFill (Colours::transparentBlack);
                    g.fillRect (bounds, true);
                    g.setFill (Colours::black);

                    paintOwner (g);
                }
            }

            owner.makeCurrentRenderingTarget();
            OpenGLHelpers::prepareFor2D (bounds.getWidth(), bounds.getHeight());
            glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glEnable (GL_BLEND);
            glColor4f (1.0f, 1.0f, 1.0f, owner.getAlpha());
            frameBuffer.drawAt (0, (float) (bounds.getHeight() - frameBuffer.getHeight()));
        }

        owner.swapBuffers();
        owner.releaseAsRenderingTarget();
    }

    void triggerRepaint()
    {
        if (! owner.isUsingDedicatedThread())
            startTimer (1000 / 70);
    }

private:
    void paintOwner (OpenGLRenderer& glRenderer)
    {
        Graphics g (&glRenderer);

       #if JUCE_ENABLE_REPAINT_DEBUGGING
        g.saveState();
       #endif

        JUCE_TRY
        {
            owner.paintEntireComponent (g, false);
        }
        JUCE_CATCH_EXCEPTION

       #if JUCE_ENABLE_REPAINT_DEBUGGING
        // enabling this code will fill all areas that get repainted with a colour overlay, to show
        // clearly when things are being repainted.
        g.restoreState();

        static Random rng;
        g.fillAll (Colour ((uint8) rng.nextInt (255),
                           (uint8) rng.nextInt (255),
                           (uint8) rng.nextInt (255),
                           (uint8) 0x50));
       #endif
    }

    OpenGLComponent& owner;
    OpenGLFrameBuffer frameBuffer;
    RectangleList validArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenGLCachedComponentImage);
};

//==============================================================================
OpenGLComponent::OpenGLComponent (const int flags_)
    : flags (flags_),
      contextToShareListsWith (nullptr),
      needToUpdateViewport (true),
      needToDeleteContext (false),
      threadStarted (false)
{
    setOpaque (true);
    componentWatcher = new OpenGLComponentWatcher (*this);
    setCachedComponentImage (new OpenGLCachedComponentImage (*this));
}

OpenGLComponent::~OpenGLComponent()
{
    stopBackgroundThread();
    componentWatcher = nullptr;
}

void OpenGLComponent::setPixelFormat (const OpenGLPixelFormat& formatToUse)
{
    if (! (preferredPixelFormat == formatToUse))
    {
        const ScopedLock sl (contextLock);
        preferredPixelFormat = formatToUse;
        recreateContextAsync();
    }
}

void OpenGLComponent::shareWith (OpenGLContext* c)
{
    if (contextToShareListsWith != c)
    {
        const ScopedLock sl (contextLock);
        contextToShareListsWith = c;
        recreateContextAsync();
    }
}

void OpenGLComponent::recreateContextAsync()
{
    const ScopedLock sl (contextLock);
    needToDeleteContext = true;
    repaint();
}

bool OpenGLComponent::makeCurrentRenderingTarget()
{
    return context != nullptr && context->makeActive();
}

void OpenGLComponent::releaseAsRenderingTarget()
{
    if (context != nullptr)
        context->makeInactive();
}

void OpenGLComponent::swapBuffers()
{
    if (context != nullptr)
        context->swapBuffers();
}

void OpenGLComponent::updateContext()
{
    if (needToDeleteContext)
        deleteContext();

    if (context == nullptr)
    {
        const ScopedLock sl (contextLock);

        if (context == nullptr)
        {
            context = createContext();

            if (context != nullptr)
            {
               #if JUCE_LINUX
                if (! isUsingDedicatedThread())
               #endif
                    updateContextPosition();

                if (context->makeActive())
                {
                    newOpenGLContextCreated();
                    context->makeInactive();
                }
            }
        }
    }
}

void OpenGLComponent::deleteContext()
{
    const ScopedLock sl (contextLock);
    if (context != nullptr)
    {
        if (context->makeActive())
        {
            releaseOpenGLContext();
            context->makeInactive();
        }

        context = nullptr;
        setCachedComponentImage (nullptr);
    }

    needToDeleteContext = false;
}

void OpenGLComponent::updateContextPosition()
{
    needToUpdateViewport = true;

    if (getWidth() > 0 && getHeight() > 0)
    {
        Component* const topComp = getTopLevelComponent();

        if (topComp->getPeer() != nullptr)
        {
            const ScopedLock sl (contextLock);
            updateEmbeddedPosition (topComp->getLocalArea (this, getLocalBounds()));
        }
    }
}

void OpenGLComponent::stopBackgroundThread()
{
    if (threadStarted)
    {
        stopRenderThread();
        threadStarted = false;
    }
}

bool OpenGLComponent::renderAndSwapBuffers()
{
    const ScopedLock sl (contextLock);

   #if JUCE_LINUX
    updateContext();
   #endif

    if (context != nullptr)
    {
        if (! makeCurrentRenderingTarget())
            return false;

        if (needToUpdateViewport)
        {
            needToUpdateViewport = false;
            glViewport (0, 0, getWidth(), getHeight());
        }

        renderOpenGL();
        swapBuffers();
    }

    return true;
}

void OpenGLComponent::triggerRepaint()
{
    OpenGLCachedComponentImage* const c
        = dynamic_cast<OpenGLCachedComponentImage*> (getCachedComponentImage());

    jassert (c != nullptr); // you mustn't set your own cached image object for an OpenGLComponent!

    if (c != nullptr)
        c->triggerRepaint();
}

void OpenGLComponent::paint (Graphics&)
{
}

unsigned int OpenGLComponent::getFrameBufferID() const
{
    return context != nullptr ? context->getFrameBufferID() : 0;
}

END_JUCE_NAMESPACE
