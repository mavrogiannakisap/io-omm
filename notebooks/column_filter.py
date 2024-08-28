import pandas as pd
import sys, os

def keep_columns(input_file, output_file, columns_to_keep):
    # Read the CSV file into a pandas DataFrame
    df = pd.read_csv(input_file)
    
    # Keep only the specified columns
    df = df[columns_to_keep]
    
    # Write the selected columns to a new CSV file
    df.to_csv(output_file, index=False)

# Specify the input and output file paths
input_directory = sys.argv[1]
# input_file_save = input_.split('/')[1]
if not os.path.exists(f"filtered/{input_directory}"):
    os.mkdir(f"filtered/{input_directory}")
for root, _, files in os.walk(input_directory):
    for input_file in files:
        print(input_file)
        output_file = f'filtered/{input_directory}/fil-{input_file}'

        if os.path.exists(output_file):
            continue
        # Specify the columns to keep
        columns_to_keep = ['id', 'append']

        # Call the function to keep columns and write to the output file
        keep_columns(f"{input_directory}/{input_file}", output_file, columns_to_keep)
