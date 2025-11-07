  If someone wants to paste your bot into their Desdemona framework:
https://plakshauniversity1-my.sharepoint.com/personal/subham_jalan_plaksha_edu_in/_layouts/15/onedrive.aspx?id=%2Fpersonal%2Fsubham%5Fjalan%5Fplaksha%5Fedu%5Fin%2FDocuments%2FDesdemona%2Ezip&parent=%2Fpersonal%2Fsubham%5Fjalan%5Fplaksha%5Fedu%5Fin%2FDocuments&ga=1
   1. **Navigate to the bots directory:**

   bash
     cd Desdemona/bots/
     mkdir MyBot
     cd MyBot

   2. **Copy these 3 files:**
   •  MyBot.cpp 
   •  MyBot.h 
   •  Makefile 

   3. **Build the framework first (from Desdemona root):**

   bash
     cd ../..  # Back to Desdemona root
     make clean && make

   4. **Build your bot:**

   bash
     cd bots/MyBot
     make clean && make

   5. **Test it:**

   bash
     cd ../..  # Back to Desdemona root
     ./bin/Desdemona ./bots/MyBot/MyBot.so ./bots/RandomBot/RandomBot.so

   ──────────────────────────────────────────

   Quick Build (One Command)

   From the project root:


   bash
     docker run --platform=linux/amd64 --rm -v $(pwd):/app -w /app/Desdemona gcc:latest bash -c "make && cd bots/MyBot && make"

   That's it! The bot is ready to use.

