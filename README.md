# Project X

- Name: Meier Simpson
- Email: meiersimpson@u.boisestate.edu
- Class: CS425-001

## Usage:

AWS test default: ./scripts/test-aws-client.py --subject "hello" --body "Test message"
AWS test override: ./scripts/test-aws-client.py --subject "hello" --body "Test message"

Local test: ./scripts/test-client.py

## Design
The three layer split in task two creates a protocol helpers layer, SMTP session layer and socket transport layer. 
The reason why it's designed like this is to allows for testing that doesn't rely on a live server.

## Experience

Overall the project docs were very helpful with developing this project. It was definitely correct that Task 2 was most of the design work for this project. Having a good understanding as to why we split lab.h into three layers was the most important part for understanding how to proceed with the rest of the project. One weird thing was that the create report github action wasn't showing up. I didn't edit the file at all but for some reason the spacing was messed up causing it to be hidden in the actions tab. I updated the spacing on line 3 and now it works.
