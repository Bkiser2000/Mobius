mkdir -p test/fixtures
cat > test/fixtures/Makefile <<'EOF'
# Generate test fixtures
.PHONY: all clean

all: training.csv score.csv

training.csv:
    @python3 <<'PYTHON'
import csv
import random

random.seed(42)
with open('training.csv', 'w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['age', 'workclass', 'income', 'education', 'approved'])
    for _ in range(100):
        age = random.randint(18, 75)
        workclass = random.choice(['Private', 'Government', 'Self-emp'])
        income = random.randint(15000, 150000)
        education = random.choice(['HS-grad', 'Bachelors', 'Masters'])
        approved = 1 if income > 50000 else 0
        writer.writerow([age, workclass, income, education, approved])
PYTHON
    @echo "Generated training.csv"

score.csv:
    @python3 <<'PYTHON'
import csv
import random

random.seed(123)
with open('score.csv', 'w', newline='') as f:
    writer = csv.writer(f)
    for _ in range(10):
        age = random.randint(18, 75)
        workclass = random.choice(['Private', 'Government', 'Self-emp'])
        income = random.randint(15000, 150000)
        education = random.choice(['HS-grad', 'Bachelors', 'Masters'])
        writer.writerow([age, workclass, income, education])
PYTHON
    @echo "Generated score.csv"

clean:
    rm -f training.csv score.csv

EOF