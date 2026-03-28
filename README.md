---------------------------  PROJECT-1  ---------------------------

Title: Poker Tournament Bracket and Elimination Engine

Problem Statement:
Simulate a knockout poker tournament where players register and compete in elimination rounds. After each round, losing players are removed from the tournament until one champion remains. The system must manage pot accumulation, ranking, and round transitions.

Core Requirements:
• Player registration and bracket formation
• Round-wise elimination handling
• Pot calculation and distribution
• Ranking maintenance
• Final winner declaration

Constraints:
• Tournament must end with exactly one winner
• No duplicate participation in same round
• Efficient restructuring after eliminations
• Maintain consistent ranking order


---------------------------  PROJECT-2  ---------------------------
Title: Predictive Crime Intelligence and Criminal Network Analysis System 

Description:
• Design and develop a Predictive Crime Intelligence and Criminal Network Analysis System for metropolitan law enforcement agencies.
• Record crime incidents and criminal profiles.
• Analyze crime patterns, detect organized crime networks, predict high-risk zones, and optimize patrol deployment.
• Integrate historical crime data, real-time incident reports, suspect link analysis, and geographical clustering to generate actionable intelligence.
• Support structured multi-stage investigations, dynamic suspect prioritization, and controlled rollback of investigative updates.
• Maintain strict data integrity and performance constraints under heavy data loads.

Core Requirements:
• Maintain structured crime records including type, severity, timestamp, geolocation, involved suspects, and investigation status.
• Manage criminal profiles with complete offense history and interconnections between suspects.
• Detect repeat offenders and generate risk scores based on past activity.
• Analyze crime clusters geographically to identify high-risk zones.
• Support multi-stage investigation workflows such as reported, evidence collection, suspect identification, interrogation, and case closure.
• Dynamically prioritize cases based on severity and risk index.
• Maintain a structured action log for investigative updates with rollback capability for the most recent modification.
• Generate predictive analytics reports including crime trend forecasting and hotspot mapping.
• Ensure synchronization between incident data, suspect profiles, and investigation logs.

Constraints:
• The system must support at least 1,000 active crime records and 5,000 criminal profiles without performance degradation.
• Crime pattern analysis and hotspot detection must execute within a fixed processing window (e.g., under 5 seconds per analytical batch).
• No criminal profile can be deleted once linked to an active or closed case.
• Only the most recent investigative update per case can be reversed, and rollback must not affect linked analytical results.
• The system must prevent cyclic or invalid links in criminal network relationships.
• Data consistency must be maintained across incident records, suspect networks, and geographic clustering modules during concurrent updates.
• Predictive risk scoring must operate within predefined computational limits suitable for deployment on mid-scale law enforcement infrastructure.
• Duplicate crime IDs, suspect IDs, or case references must be automatically rejected.
• The system must continue functioning during peak crime-reporting periods without memory overflow or loss of high-priority alerts.