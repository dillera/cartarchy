#!/usr/bin/env python3
"""Build the Cartarchy for the Amiga player's manual (PDF).

Screenshots in shots/ are cropped from Amiberry; the logo is the game's own
art/logo.png. Needs reportlab and Pillow:

    uv run --with reportlab --with pillow manual/mkmanual.py
"""
import io
import os

from PIL import Image
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (Image as RLImage, KeepTogether, PageBreak, Paragraph,
                                SimpleDocTemplate, Spacer, Table, TableStyle)

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, "shots")
OUT = os.path.join(HERE, "Cartarchy-Amiga-Manual.pdf")

NAVY = colors.HexColor("#112244")
SEA = colors.HexColor("#225588")
BRASS = colors.HexColor("#EECC22")
ORANGE = colors.HexColor("#FF8822")
INK = colors.HexColor("#1A1A22")
PANEL = colors.HexColor("#E6E6EE")
RULE = colors.HexColor("#8888AA")

# Copperplate for headings when this Mac has it, as in the game's logo
HEAD = "Helvetica-Bold"
try:
    pdfmetrics.registerFont(TTFont("Copperplate", "/System/Library/Fonts/Supplemental/Copperplate.ttc",
                                   subfontIndex=2))
    HEAD = "Copperplate"
except Exception:
    pass

body = ParagraphStyle("body", fontName="Helvetica", fontSize=10.5, leading=14.5, textColor=INK,
                      spaceAfter=7)
small = ParagraphStyle("small", parent=body, fontSize=9, leading=12, textColor=colors.HexColor("#444455"))
caption = ParagraphStyle("caption", parent=small, alignment=TA_CENTER, spaceBefore=3, spaceAfter=10)
h1 = ParagraphStyle("h1", fontName=HEAD, fontSize=20, leading=24, textColor=NAVY, spaceBefore=4,
                    spaceAfter=10)
h2 = ParagraphStyle("h2", fontName=HEAD, fontSize=13.5, leading=17, textColor=SEA, spaceBefore=10,
                    spaceAfter=5)
bullet = ParagraphStyle("bullet", parent=body, leftIndent=14, bulletIndent=3, spaceAfter=3)
cell = ParagraphStyle("cell", parent=body, fontSize=9.5, leading=12, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Helvetica-Bold")
tip = ParagraphStyle("tip", parent=body, fontSize=9.5, leading=13, textColor=NAVY, spaceAfter=0)


def P(text, style=body):
    return Paragraph(text, style)


def bullets(items):
    return [Paragraph(t, bullet, bulletText="•") for t in items]


def pixel_image(path, width, scale_y=1, upscale=2):
    """An Amiga picture, enlarged with hard pixel edges."""
    im = Image.open(path).convert("RGB")
    im = im.resize((im.width * upscale, im.height * scale_y * upscale), Image.NEAREST)
    buf = io.BytesIO()
    im.save(buf, "PNG")
    buf.seek(0)
    height = width * im.height / im.width
    return RLImage(buf, width=width, height=height)


def shot(name, text, width=5.6 * inch):
    frame = Table([[pixel_image(os.path.join(SHOTS, name + ".png"), width)]],
                  style=[("BOX", (0, 0), (-1, -1), 1.2, NAVY),
                         ("LEFTPADDING", (0, 0), (-1, -1), 0), ("RIGHTPADDING", (0, 0), (-1, -1), 0),
                         ("TOPPADDING", (0, 0), (-1, -1), 0), ("BOTTOMPADDING", (0, 0), (-1, -1), 0)])
    return KeepTogether([frame, P(text, caption)])


def table(rows, widths, header=True):
    data = [[P(c, cellb if (header and r == 0) else cell) for c in row] for r, row in enumerate(rows)]
    style = [("GRID", (0, 0), (-1, -1), 0.5, RULE), ("VALIGN", (0, 0), (-1, -1), "TOP"),
             ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4)]
    if header:
        style.append(("BACKGROUND", (0, 0), (-1, 0), PANEL))
    return Table(data, colWidths=widths, style=style, hAlign="LEFT")


def note(text):
    return Table([[P(text, tip)]], colWidths=[6.5 * inch],
                 style=[("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#FFF6D8")),
                        ("BOX", (0, 0), (-1, -1), 0.8, BRASS),
                        ("LEFTPADDING", (0, 0), (-1, -1), 8), ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                        ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)])


def cover(c, doc):
    w, h = letter
    # The copper sea: bands darkening towards the poles
    stops = [(0.0, (0x01, 0x01, 0x03)), (0.2, (0x01, 0x03, 0x05)), (0.52, (0x02, 0x05, 0x08)),
             (0.8, (0x01, 0x04, 0x06)), (1.0, (0x00, 0x01, 0x03))]
    bands = 48
    for i in range(bands):
        t = i / (bands - 1)
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t0 <= t <= t1:
                k = (t - t0) / (t1 - t0)
                rgb = [round(a + (b - a) * k) * 17 / 255 for a, b in zip(c0, c1)]
                break
        c.setFillColorRGB(*rgb)
        c.rect(0, h - (i + 1) * h / bands, w, h / bands + 1, stroke=0, fill=1)
    # The logo, its hires pixels twice as tall as wide
    logo = Image.open(os.path.join(HERE, "..", "art", "logo.png")).convert("RGB")
    logo = logo.resize((logo.width * 3, logo.height * 6), Image.NEAREST)
    lw = 6.2 * inch
    lh = lw * logo.height / logo.width
    c.setFillColor(colors.HexColor("#888899"))
    c.roundRect((w - lw) / 2 - 14, h - 4.6 * inch - 14, lw + 28, lh + 28, 10, stroke=0, fill=1)
    c.drawImage(ImageReader(logo), (w - lw) / 2, h - 4.6 * inch, lw, lh)
    c.setFillColor(colors.white)
    c.setFont(HEAD, 26)
    c.drawCentredString(w / 2, h - 5.55 * inch, "for the Amiga")
    c.setFillColor(BRASS)
    c.setFont(HEAD, 15)
    c.drawCentredString(w / 2, h - 6.05 * inch, "A Game of Global Strategy")
    c.setFillColor(colors.HexColor("#CCCCDD"))
    c.setFont("Helvetica", 12)
    c.drawCentredString(w / 2, h - 6.9 * inch, "Player's Manual")
    c.setFont("Helvetica", 10)
    c.drawCentredString(w / 2, 1.2 * inch, "Workbench 1.3 and later  •  any Amiga from the A500  •  FujiNet NIO")


def later(c, doc):
    w, h = letter
    c.setStrokeColor(NAVY)
    c.setLineWidth(0.8)
    c.line(inch, h - 0.7 * inch, w - inch, h - 0.7 * inch)
    c.setFont(HEAD, 9)
    c.setFillColor(NAVY)
    c.drawString(inch, h - 0.62 * inch, "Cartarchy for the Amiga")
    c.drawRightString(w - inch, h - 0.62 * inch, "Player's Manual")
    c.setFont("Helvetica", 9)
    c.drawCentredString(w / 2, 0.55 * inch, str(doc.page))


def story():
    s = [PageBreak()]

    s += [P("Welcome, Commander", h1),
          P("<b>Cartarchy</b> is a game of world conquest. Up to six commanders share a map of 42 "
            "territories on six continents. Each turn you raise armies, attack your neighbours with "
            "the dice, and move your forces up to the front. The last commander standing rules the world."),
          P("The Amiga edition plays two ways:"),
          *bullets(["<b>Campaign against the computer</b>: you against one to five computer commanders, "
                    "with no network needed beyond the FujiNet that starts the game.",
                    "<b>Online on a Cartarchy server</b>: turn-by-turn games against friends on other "
                    "Amigas, iPhones and Android phones, all on the same board and rules."]),
          Spacer(1, 6),
          P("What you need", h2),
          table([["Item", "Notes"],
                 ["An Amiga", "Any model from the A500 (68000) up, Kickstart and Workbench 1.3 or later. "
                  "1 MB of memory (512 KB chip RAM plus 512 KB more) is plenty."],
                 ["A FujiNet", "Running the FujiNet NIO firmware, connected to the Amiga and to your network."],
                 ["fujinet-nio.device", "The FujiNet driver, in <font face='Courier'>DEVS:</font>, on the "
                  "<font face='Courier'>NIO:</font> drive, or beside the game."],
                 ["For online play", "The address of a Cartarchy server, e.g. "
                  "<font face='Courier'>https://cartarchy.diller.org</font>, and its pairing code."]],
                [1.6 * inch, 4.9 * inch]),
          Spacer(1, 8),
          P("Installing and starting", h2),
          P("Copy <b>Cartarchy</b> and its icon <b>Cartarchy.info</b> to any disk or drawer. The game is "
            "also on the <i>DILLER Games 1</i> floppy and on the FujiNet <font face='Courier'>NIO:</font> "
            "drive. Double-click the globe-and-swords icon to start. From a Shell, type "
            "<font face='Courier'>Cartarchy</font>. The game finds and loads "
            "<font face='Courier'>fujinet-nio.device</font> itself if it is not already running."),
          shot("workbench", "Cartarchy on the NIO: drive in Workbench 1.3", 3.6 * inch),
          ]

    s += [PageBreak(), P("The Main Menu", h1),
          P("The title screen shows a living world: empires spread across the map behind the menu. "
            "The panel on the right shows the server you are paired with and your commander name. The "
            "status line at the bottom tells you whether the FujiNet answered."),
          shot("menu", "The main menu"),
          table([["Button", "Key", "What it does"],
                 ["PLAY ON THE SERVER", "S", "Lists your online games and open lobbies (pair first)."],
                 ["CAMPAIGN VS COMPUTER", "C", "Starts a game against computer commanders."],
                 ["PAIR WITH A SERVER", "P", "Sets the server, pairs this Amiga with it, and renames you."],
                 ["QUIT", "Q", "Back to Workbench."]],
                [2.0 * inch, 0.6 * inch, 3.9 * inch]),
          ]

    s += [PageBreak(), P("Playing Online", h1),
          P("Pairing with a server", h2),
          P("Before your first online game, pair this Amiga with a Cartarchy server. You only need to do "
            "this once: the FujiNet remembers the server and your sign-in, even after a reboot."),
          *bullets(["Choose <b>PAIR WITH A SERVER</b>.",
                    "Type the server address in <b>SERVER ADDRESS</b>. Press Ctrl-X to clear a field.",
                    "Type the <b>PAIRING CODE</b>. It is shown on the server's web page, and the server's "
                    "operator can tell you.",
                    "Click <b>PAIR</b>. The server welcomes you under a commander name of its choosing, "
                    "such as Artemis or Juno.",
                    "To pick your own name, type it in <b>COMMANDER NAME</b> and click <b>RENAME</b>."]),
          shot("pairing", "Pairing: the server address and its pairing code, typed in"),
          note("<b>Tip:</b> Tab moves between fields and Return goes to the next one. The name you choose "
               "is the one other players see, on every device."),
          ]

    s += [PageBreak(), P("Server games", h2),
          P("<b>PLAY ON THE SERVER</b> lists your games and every open lobby. Columns show the game's "
            "name, the seats taken, and whose turn it is. <font color='#CC2222'><b>YOURS!</b></font> "
            "means the server is waiting for you. The list refreshes itself every 20 seconds, or click "
            "<b>REFRESH</b>."),
          shot("games", "The games list: a lobby waiting to start, and a game on your turn"),
          table([["Row button", "Meaning"],
                 ["PLAY / RESUME", "Open a game you are in. RESUME means it is already under way."],
                 ["JOIN", "Take a seat in an open lobby."],
                 ["START", "You made this lobby and at least two seats are filled: start the game now. "
                  "A lobby also starts by itself when every seat is taken."],
                 ["WAITING", "Your lobby still needs more players."],
                 ["VIEW", "Look at a finished game."]],
                [1.4 * inch, 5.1 * inch]),
          ]

    s += [PageBreak(), P("Starting and joining games", h2),
          P("<b>NEW GAME</b> creates a lobby. Give it a name, choose 2 to 6 seats, and choose how turns "
            "work. <b>TURN BY TURN</b> lets only the commander whose turn it is move. <b>ANY TIME</b> is "
            "the server's real-time mode. When the lobby is made, the status line shows its "
            "<b>join code</b>, e.g. <font face='Courier'>B86D3F1F</font>. Send it to your friends."),
          shot("newgame", "A new three-seat game"),
          P("To join a friend's game, click <b>JOIN CODE</b> and type the code they sent you. Lobbies also "
            "appear in everyone's list with a JOIN button."),
          P("While you wait", h2),
          P("When it's another commander's turn, the status line reads <b>WAITING FOR</b> their name. The "
            "Amiga checks the server every few seconds and shows each move as it lands. A chime sounds "
            "when it's your turn again. You can leave with <b>GAMES</b> at any time and come back later: "
            "the server keeps the game."),
          note("<b>About speed:</b> every move sends the whole game, about 5 KB, so a move takes a few "
               "seconds at the FujiNet's standard 19200 baud. Clicks made close together on your turn "
               "are sent as one. If your FujiNet and Amiga are happy at a higher speed, set it with "
               "<font face='Courier'>fujinet-nio-baud</font>."),
          ]

    s += [PageBreak(), P("Campaign Against the Computer", h1),
          P("Choose how many computer commanders to face (1 to 5), and click each one's button to set "
            "its temperament:"),
          *bullets(["<b>CAUTIOUS</b> attacks only with an advantage of three armies or more.",
                    "<b>BALANCED</b> and <b>AGGRESSIVE</b> attack whenever they have more armies than "
                    "the land they attack."]),
          Spacer(1, 4),
          shot("campaign", "Setting up a campaign against three computer commanders"),
          P("You are always the red commander and move first. The computers move by themselves, and you "
            "can watch every claim, attack and capture."),
          ]

    s += [PageBreak(), P("The Game Screen", h1),
          shot("board", "A game in progress"),
          table([["Area", "What it shows"],
                 ["Top bar", "The turn number, or SETUP, and the game's name (or CAMPAIGN)."],
                 ["The map", "Each land is filled with its owner's colour, with a badge showing its armies. "
                  "Dashed lines are sea routes. Alaska and Kamchatka are neighbours across the edge of "
                  "the map."],
                 ["Side panel, top", "Whose turn it is, and the phase: CLAIM A LAND, PLACE ARMY, "
                  "REINFORCE (with the armies left to place), ATTACK or FORTIFY."],
                 ["Commander list", "Each commander's colour, name, lands (LND) and armies (ARM). The "
                  "commander to move is in white; knocked-out commanders show OUT."],
                 ["Land details", "Point at a land to see its name, its continent and bonus, its owner "
                  "and armies."],
                 ["CARDS", "How many cards you hold. SET! means you can trade a set."],
                 ["Buttons", "The phase's action (RANDOM SETUP, TRADE SET, END ATTACKS or END TURN), then "
                  "GAMES or MENU, and RESIGN in online games."],
                 ["Status line", "What just happened, the last dice roll, and YOUR TURN or who you are "
                  "waiting for."]],
                [1.4 * inch, 5.1 * inch]),
          ]

    s += [PageBreak(), P("How to Play", h1),
          P("1. Setting up the world", h2),
          P("In turn, each commander claims one empty land (<b>CLAIM A LAND</b>) until all 42 are taken. "
            "Then you take turns placing your remaining armies one at a time on lands you own "
            "(<b>PLACE ARMY</b>). Starting armies depend on the number of players: 40 each for two, 35 "
            "for three, 30 for four, 25 for five and 20 for six."),
          P("In a hurry? <b>RANDOM SETUP</b>, on your turn during setup, deals every land and army at "
            "random and starts the first turn straight away."),
          shot("claim", "Claiming lands: three computer commanders have answered your first claim",
               4.6 * inch),
          P("2. Reinforce", h2),
          P("Each turn starts with new armies: your lands divided by three (at least 3), plus a bonus for "
            "each continent you hold completely. Click your lands to place them one at a time."),
          table([["Continent", "Lands", "Bonus"],
                 ["Asia", "12", "+7"], ["North America", "9", "+5"], ["Europe", "7", "+5"],
                 ["Africa", "6", "+3"], ["South America", "4", "+2"], ["Australia", "4", "+2"]],
                [2.2 * inch, 0.9 * inch, 0.9 * inch]),
          ]

    s += [PageBreak(),
          P("Cards", h2),
          P("If you capture at least one land during your turn, you draw a card when the turn ends. Each "
            "card shows a land and a unit (infantry, cavalry or artillery), and two are wild. During "
            "reinforcement, <b>TRADE SET</b> turns three cards into extra armies. A set is three of one "
            "unit, one of each unit, or any set with a wild card."),
          *bullets(["Sets are worth 4, 6, 8, 10, 12 and 15 armies, then 5 more for each later trade, "
                    "counted across all commanders.",
                    "If a traded card shows a land you own, that land gets 2 extra armies.",
                    "Holding five cards, you must trade before placing armies."]),
          P("3. Attack", h2),
          P("Click one of your lands with at least two armies. It shows in white check, and every land "
            "it can attack pulses. Click an enemy neighbour to roll the dice. Click it again to attack "
            "again. Right-click or Esc picks a different land."),
          shot("attack", "Ukraine picked: its enemy neighbours pulse, ready to be attacked"),
          ]

    s += [PageBreak(),
          table([["Dice", "Rule"],
                 ["Attacker", "Rolls one die per army beyond the first, up to three (red dice)."],
                 ["Defender", "Rolls one die per army, up to two (white dice)."],
                 ["Result", "The highest dice are compared, then the next highest. For each pair the "
                  "higher die wins, and the defender wins ties. Each loss costs one army."],
                 ["Capture", "When the last defender falls, the land is yours. As many armies as the "
                  "dice you rolled move in, leaving at least one behind."]],
                [1.2 * inch, 5.3 * inch]),
          Spacer(1, 8),
          shot("dice", "Three red dice against one: \"Rolled 5,3,3 vs 6.\" The defender's six holds"),
          P("When you've attacked enough, click <b>END ATTACKS</b> (or press E)."),
          P("4. Fortify", h2),
          P("You may now strengthen your front once. Click one of your lands with more than one army, "
            "then a neighbouring land of yours: all but one army march over, and your turn ends. "
            "Otherwise click <b>END TURN</b>."),
          P("Winning", h2),
          P("A commander who loses their last land is out, and their cards go to the conqueror. The last "
            "commander left conquers the world. In an online game you can also <b>RESIGN</b>. Click it "
            "twice, as it asks <b>SURE?</b> first. In a two-player game, resigning hands your opponent "
            "the win."),
          ]

    s += [PageBreak(), P("Quick Reference", h1),
          table([["To do this", "Mouse", "Keys"],
                 ["Claim, place, reinforce, pick a land, attack, move", "Click the land or its badge", ""],
                 ["Cancel a pick", "Right button", "Esc"],
                 ["End attacks / end turn", "END ATTACKS / END TURN", "E or Space"],
                 ["Trade a card set", "TRADE SET +n", "T"],
                 ["Deal everything at random (setup)", "RANDOM SETUP", "R"],
                 ["Back to the games list or menu", "GAMES / MENU", "G, or Esc with nothing picked"],
                 ["Menu buttons", "Click", "First letter (S, C, P, Q, N, J...)"],
                 ["Clear a text field", "", "Ctrl-X"],
                 ["Next text field", "Click the field", "Tab or Return"]],
                [2.8 * inch, 2.0 * inch, 1.7 * inch]),
          Spacer(1, 10),
          shot("online", "An online game: your turn to reinforce, after the other commander's turn arrived",
               4.6 * inch),
          ]

    s += [PageBreak(), P("Troubleshooting", h1),
          table([["Message", "What to do"],
                 ["NO FUJINET-NIO.DEVICE IN DEVS: OR NIO:", "Copy fujinet-nio.device to DEVS: or next to "
                  "the game. The campaign still works without it."],
                 ["FUJINET IS NOT ANSWERING", "Check that the FujiNet is powered and connected, then "
                  "start the game again."],
                 ["NOT PAIRED WITH THIS SERVER: PAIR AGAIN", "The server no longer knows this Amiga. Use "
                  "PAIR WITH A SERVER with its current pairing code."],
                 ["SERVER: INVALID PAIRING CODE OR DEVICE ID", "The pairing code is wrong or has changed. "
                  "Ask the server's operator."],
                 ["THE GAME MOVED ON: HERE IS THE LATEST.", "Someone else changed the game first. The "
                  "board now shows the server's copy, so play on from there."],
                 ["NOT YOUR TURN YET.", "Wait for the commander named in the status line."],
                 ["Moves take several seconds", "Normal at 19200 baud. A faster link "
                  "(fujinet-nio-baud) speeds things up."],
                 ["Cartarchy needs about 180K of free chip memory.", "Close other programs or screens. "
                  "The game needs about 210 KB of chip RAM."]],
                [2.7 * inch, 3.8 * inch]),
          Spacer(1, 14),
          P("Credits", h2),
          P("Cartarchy was created by Andrew Diller, with editions for iOS, Android and the Amiga, and its "
            "own game server. The Amiga edition talks to the server through FujiNet NIO."),
          P("The territory outlines are adapted from “Risk board.svg” by Gr0gmint, licensed "
            "under Creative Commons Attribution-ShareAlike 3.0 "
            "(creativecommons.org/licenses/by-sa/3.0).", small),
          ]
    return s


def main():
    doc = SimpleDocTemplate(OUT, pagesize=letter, leftMargin=inch, rightMargin=inch,
                            topMargin=0.95 * inch, bottomMargin=0.85 * inch,
                            title="Cartarchy for the Amiga: Player's Manual", author="Andrew Diller",
                            subject="Setting up and playing Cartarchy on the Amiga")
    doc.build(story(), onFirstPage=cover, onLaterPages=later)
    print(OUT)


if __name__ == "__main__":
    main()
