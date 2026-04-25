UX Guidelines
=============

Designing effective and fun hardware for music-making requires balancing
functionality with immediate playability. This can be especially hard to
do when you have just worked on something for a long time and are
biassed because of that, but also if you have never done it before. Here
are a few tips to help you create an interface that is easy to adapt for
musicians. It is interesting to see how many of these concepts are
mentioned or described in our artist interviews (`check them out
here <https://dadamachines.github.io/ctag-tbd/interviews/index.html>`__);
however they are not absolute rules, but the more you stick to them, the
more playable your device will become.

TLDR – the short version
~~~~~~~~~~~~~~~~~~~~~~~~~

**Map the navigation** – Sketch a 2D diagram of pages/hierarchy; count
clicks to reach each feature.

**Limit cognitive load** – Chunk info into 4–9 items; players already
juggle pitch, timing, bandmates.

**Support flow** – The device should disappear; no thinking about where
things are.

**Consistent navigation** – One pattern for entering
submenus/confirming, applied everywhere.

**Show only what matters** – Hide rarely-used parameters deeper; avoid
parameter chaos.

**Get outside testers** – Don’t explain it; watch them stumble. Painful
but essential.

**Musical, not technical ranges** – Map to sweet spots, use log scales
for freq/volume, tune parameters together (e.g. Cutoff + Resonance).

**Keep hierarchy flat** – Critical controls on top layer; fewer pages =
faster learning.

**Standard gestures** – Clockwise = increase, left→right signal flow,
call the modifier “Shift”.

Map out your instruments navigation in a diagram
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This is more of a concrete step than a general principle, but it can
help a lot in identifying issues in terms of hierarchy and navigation
that otherwise might not be so obvious. Try to sketch out your device
visually in a 2D diagram. This helps you to organize what´s where, how
many pages or hierarchy levels you need and if the plan in your head is
actually something that also works in real life. This can identify flaws
you didn´t know were there, but also help you validate your layout and
answer important questions: when i do this, how do i get to that
functionality? It also helps to actually count clicks and turns to see
how far features are apart from each other.

Limit cognitive load
~~~~~~~~~~~~~~~~~~~~

This is kind of a meta-concept that bleeds into many other aspects of
musical user interfaces mentioned later, but is very important to
remember. Apply it wherever you can. When making music, people have to
keep track of many things at the same time: the pitch, the timing, other
music coming from other sources such as people or machines you play
with, even an audience and its emotion if the music making takes place
on a stage. This is all on top of playing your own instrument, so a lot
to process. Human short-term memory is limited to a small number of 4-9
items, so you need to chunk your information into digestible bits that
belong together.

Strengthen the flow-state
~~~~~~~~~~~~~~~~~~~~~~~~~

That´s another meta-concept important on many levels of instrument
design and tightly intertwined with a low cognitive load. If you don´t
have to actively “think” about where which parameter might be and how to
reach it, but are intuitively guided there, it is much more fun and
intuitive. Flow occurs when the device becomes “invisible” and you just
jam on without actively thinking about what you are doing.

Use consistent navigation
~~~~~~~~~~~~~~~~~~~~~~~~~

This is important in making it easy for the user to quickly establish a
sense of location and navigation on your device. if they remember how to
enter one feature / feature set and they are confronted with a similar
situation and can apply the same pattern, everything is much smoother.
Use the existing user interface in a way that leverages patterns we all
know and use everyday: up and down, left to right, etc. Ensure that
navigation patterns—such as how to enter a submenu or confirm a
setting—are identical across the entire device, or as similar as
possible. Once you have decided on a pattern/several patterns, be as
consistent as you can throughout your device in using them.

Only show important information
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are many examples for big synthesizers, workstations, DAWs and
other apps that show an abundance of information at once (also sometimes
in a weird mix of parameters that don´t really belong together), all
screaming for attention. (decision paralysis) There are probably a lot
of things and options that you could access on your device that are of
little to no use to the musician that wants to play it. If you are
unsure about such a parameter, most of the time it´s highly likely that
it´s not that important and you might want to “hide” it further down in
the hierarchy. This is where it´s also very helpful to have someone else
test and play with this area.

Have someone else test your instrument/effect
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Testing is an important step and allows you to step out of the position
as instrument designer in order to avoid the bias that might have been
building up while developing the device. It´s not so important how many
people you have that test your device, but rather that they have a very
different outlook on it since they were not involved. If you have the
chance to be present while the a person tests creation, don´t tell them
how it works and see if they can find out by themselves. This sometimes
can be quite challenging and even painful to sit through, but is very
helpful to identify roadblocks or wrong design decisions.

Adapt parameter ranges to the user interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Technical ranges (like 0–20,000 Hz) are very different from musical
ranges. Map your parameters to sweet spots where changes are most
audible, musically interesting and playable. You might be really proud
that you realised a super high resolution for a parameter, but it turns
out that the biggest part of that doesn´t make musical sense or is even
inaudible. If you are tuning a parameter, make sure to also use it in
conjunction with another parameter that´s likely to be used together
with it. On a filter, this could be Cutoff and Resonance. Use
logarithmic scales for volume and frequency to match how the human ear
naturally perceives sound.

Keep the hierarchy levels as flat as possible
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Prioritize a flat structure where the most critical controls are on the
top layer. This is an important element in order to guide the user
efficiently and allow them to learn your instrument properly. In this
day and age we all have reduced attention spans, so that´s another
factor that plays into keeping your hierarchies flat. The less key
combinations and hidden extra features you will have, the easier it is
to get used to, learn and as a result play your instrument. Whenever you
can avoid establishing another page or vertical hierarchy level it makes
your instrument that much more intuitive.

Standardize gestures & Don´t reinvent the wheel
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you can, try to develop a“language” that your interface speaks that
is as consistent as possible. Consistent gestures leverage muscle
memory, allowing for effortless interaction without second-guessing. For
example, clockwise turns should always increase a value, a button press
usually confirms something or initiates a switch, signal flow usually
goes from left to right and if you have a modifier button, just call it
Shift. There are lots of examples of devices where this easy principle
wasn´t followed and this creates unnecessary friction.

