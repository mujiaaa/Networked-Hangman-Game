# 🕹️ Networked Hangman Game

A multiplayer command-line Hangman game built in **C** using **TCP socket programming**.  

## Overview
This project implements a **client–server architecture** for the classic Hangman word-guessing game.

- The **server** randomly selects a secret word from a word list and manages game state for each connected client.
- The **client** allows players to guess letters, view their current progress, and receive real-time updates from the server.
- Supports up to **three simultaneous players**, with overload handling for additional connections.

## Features
- Server reads words from a file named "hangman_words.txt" and chooses a random word for each game
- Client displays game state according to the assignment requirements
- Game ends when a player guesses the word correctly or makes 6 incorrect guesses
- Server rejects connections beyond the 3-client limit with a "server-overloaded" message

## Building
- To compile the server and client programs, run: `make all`

## Running the Game
- Start the server with a port number: `./hangman_server [port]`
- Start the client with server IP and port: `./hangman_client [server_ip] [port]`
- Follow the on-screen prompts to play the game

## Notes
- The server requires a file named "hangman_words.txt" with words between 3-8 letters
- Only single letters are accepted as guesses
