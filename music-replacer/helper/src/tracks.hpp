// The game's soundtrack and theme music, by slot: names from the game's English text (language_eng.lng: lng_licensed_music_track_N,
// lng_theme_music_track_N), lengths from its banks. A slot is the Wwise switch value music_soundtrack_NN or
// music_theme_NN; its audio is found in the banks at build time.
#pragma once

struct Track {
    int slot;
    const char *title, *artist;
    int seconds;
};

inline constexpr Track kSoundtrack[] = {
    {1, "Bruk Out", "2ndstar ft. Scrufizzer", 149},
    {2, "Just A Dance (Safari Riot - Kill Dave Remix)", "Alfie Templeman ft. Nile Rodgers", 147},
    {3, "Monaco", "Anna Sofia ft. LZee", 157},
    {4, "Hypnotized", "Anyma ft. Ellie Goulding", 180},
    {5, "HOT & MYSTERIOUS", "AUSTIN MILLZ, KAH-LO", 155},
    {6, "Behind the Wheel (Depeche Mode Cover)", "Devault ft. Slayyyter", 213},
    {7, "Receipts", "Diplo & Mau P ft. Gunna", 203},
    {8, "Fuego", "Dogger & Mindstate", 201},
    {9, "Run", "Elderbrook & Vintage Culture", 171},
    {10, "Baddy On The Floor", "Jamie xx, Honey Dijon", 186},
    {11, "Shine On", "Kaskade, Wilkinson & Paige Cavell", 211},
    {12, "Interstellar", "KREAM", 287},
    {13, "Control", "Martin Wave ft. Izzy Bizu", 156},
    {14, "DNA", "Mild Minds", 203},
    {15, "Do It Like Me", "nimino", 196},
    {16, "Old Skool", "Nonô", 117},
    {17, "PINK LIKE SUKI", "Pebbles&TamTam", 150},
    {18, "Latinamerica", "Raffa FL, MichaelBM, Jayie, ft. Vika", 125},
    {19, "Never Go Back", "RBØR, Simo Moumen", 189},
    {20, "Bread (DJ Seinfeld Remix)", "SOFI TUKKER", 332},
    {21, "Darkness In Me", "Steve Angello, Modern Tales", 268},
    {22, "Push The Tempo", "Sub Focus & Katy B", 162},
    {23, "I Destroyed Disco", "The Dare", 142},
    {24, "Chariot", "The Midnight", 266},
    {25, "RIGHT NOW", "threesixnine ft. Luckhardt", 212},
    {26, "I Don't Wanna Know", "Waeys, Nathan Smoker, Verbz", 237},
    {27, "Lose My Mind", "Don Toliver & D*ja Cat", 209},
    {28, "Drive", "Ed Sheeran", 187},
    {29, "Underdog", "Roddy Ricch", 142},
    {30, "Bad As I Used To Be", "Chris Stapleton", 300},
};

inline constexpr Track kTheme[] = {
    {1, "Ghost Of Dominance", "Ian Livingstone", 269},
    {2, "Formula For Success", "Ian Livingstone", 267},
    {3, "The Long Game", "Ian Livingstone", 236},
};
